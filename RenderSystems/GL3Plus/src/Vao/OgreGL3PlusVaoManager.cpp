/*
-----------------------------------------------------------------------------
This source file is part of OGRE
(Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org

Copyright (c) 2000-2014 Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/

#include "Vao/OgreGL3PlusVaoManager.h"

namespace Ogre
{
    void GL3PlusVaoManager::allocateVbo( size_t sizeBytes, size_t bytesPerElement, BufferType bufferType,
                                         size_t &outVboIdx, size_t &outBufferOffset )
    {
        assert( bytesPerElement > 0 );

        VboFlag vboFlag = CPU_INACCESSIBLE;

        if( bufferType == BT_DYNAMIC )
        {
            sizeBytes   *= mDynamicBufferMultiplier;
            vboFlag     = CPU_ACCESSIBLE;
        }

        VboVec::const_iterator itor = mVbos[vboFlag].begin();
        VboVec::const_iterator end  = mVbos[vboFlag].end();

        //Find a suitable VBO that can hold the requested size. We prefer those free
        //blocks that have a matching stride (the current offset is a multiple of
        //bytesPerElement) in order to minimize the amount of memory padding.
        size_t bestVboIdx   = ~0;
        size_t bestBlockIdx = ~0;
        bool foundMatchingStride = false;

        while( itor != end && !foundMatchingStride )
        {
            BlockVec::const_iterator blockIt = itor->freeBlocks.begin();
            BlockVec::const_iterator blockEn = itor->freeBlocks.end();

            while( blockIt != blockEn && !foundMatchingStride )
            {
                const Block &block = *blockIt;

                //Round to next multiple of bytesPerElement
                size_t newOffset = ( (block.offset + bytesPerElement - 1) /
                                     bytesPerElement ) * bytesPerElement;

                if( sizeBytes <= block.size - (newOffset - block.offset) )
                {
                    bestVboIdx      = itor - mVbos[vboFlag].begin();
                    bestBlockIdx    = blockIt - itor->freeBlocks.begin();

                    if( newOffset == block.offset )
                        foundMatchingStride = true;
                }

                ++blockIt;
            }

            ++itor;
        }

        if( bestBlockIdx == ~0 )
        {
            bestVboIdx      = mVbos[vboFlag].size();
            bestBlockIdx    = 0;
            foundMatchingStride = true;

            Vbo newVbo;

            size_t poolSize = std::max( mDefaultPoolSize[vboFlag], sizeBytes );

            //TODO: Deal with Out of memory errors
            //No luck, allocate a new buffer.
            OCGLE( glGenBuffers( 1, &newVbo.vboName ) );
            OCGLE( glBindBuffer( GL_ARRAY_BUFFER, newVbo.vboName ) );

            if( mArbBufferStorage )
            {
                if( vboFlag == CPU_ACCESSIBLE )
                {
                    OCGLE( glBufferStorage( GL_ARRAY_BUFFER, poolSize, 0,
                                            GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT |
                                            GL_MAP_COHERENT_BIT ) );
                }
                else
                {
                    OCGLE( glBufferStorage( GL_ARRAY_BUFFER, poolSize, 0, 0 ) );
                }
            }
            else
            {
                OCGLE( glBufferData( GL_ARRAY_BUFFER, mDefaultPoolSize[vboFlag], 0,
                                     vboFlag == CPU_INACCESSIBLE ? GL_STATIC_DRAW : GL_DYNAMIC_DRAW ) );
            }
            OCGLE( glBindBuffer( GL_ARRAY_BUFFER, 0 ) );

            newVbo.sizeBytes = poolSize;
            newVbo.freeBlocks.push_back( Block( 0, poolSize ) );

            mVbos[vboFlag].push_back( newVbo );
        }

        Vbo &bestVbo        = mVbos[vboFlag][bestVboIdx];
        Block &bestBlock    = bestVbo.freeBlocks[bestBlockIdx];

        size_t newOffset = ( (bestBlock.offset + bytesPerElement - 1) /
                             bytesPerElement ) * bytesPerElement;
        size_t padding = newOffset - bestBlock.offset;
        //Shrink our records about available data.
        bestBlock.size   -= sizeBytes + padding;
        bestBlock.offset = newOffset + sizeBytes;

        if( !foundMatchingStride )
        {
            //This is a stride changer, record as such.
            StrideChangerVec::iterator itStride = std::lower_bound( bestVbo.strideChangers.begin(),
                                                                    bestVbo.strideChangers.end(),
                                                                    newOffset, StrideChanger() );
            bestVbo.strideChangers.insert( itStride, StrideChanger( newOffset, padding ) );
        }

        outVboIdx       = bestVboIdx;
        outBufferOffset = newOffset;
    }
    //-----------------------------------------------------------------------------------
    void GL3PlusVaoManager::deallocateVbo( size_t vboIdx, size_t bufferOffset, size_t sizeBytes,
                                           BufferType bufferType )
    {
        VboFlag vboFlag = bufferType == BT_DYNAMIC ? CPU_ACCESSIBLE: CPU_INACCESSIBLE;

        Vbo &vbo = mVbos[vboFlag][vboIdx];
        StrideChangerVec::iterator itStride = std::lower_bound( vbo.strideChangers.begin(),
                                                                vbo.strideChangers.end(),
                                                                bufferOffset, StrideChanger() );

        if( itStride != vbo.strideChangers.end() && itStride->offsetAfterPadding == bufferOffset )
        {
            bufferOffset    -= itStride->paddedBytes;
            sizeBytes       += itStride->paddedBytes;
        }

        //See if we're contiguous to a free block and make that block grow.
        vbo.freeBlocks.push_back( Block( bufferOffset, sizeBytes ) );
        mergeContiguousBlocks( vbo.freeBlocks.end() - 1, vbo.freeBlocks );
    }
    //-----------------------------------------------------------------------------------
    void GL3PlusVaoManager::mergeContiguousBlocks( BlockVec::iterator blockToMerge,
                                                   BlockVec &blocks )
    {
        BlockVec::iterator itor = blocks.begin();
        BlockVec::iterator end  = blocks.end();

        while( itor != end )
        {
            if( itor->offset + itor->size == blockToMerge->offset )
            {
                itor->size += blockToMerge->size;
                size_t idx = itor - blocks.begin();
                efficientVectorRemove( blocks, blockToMerge );

                mergeContiguousBlocks( blocks.begin() + idx, blocks );
                return;
            }

            if( blockToMerge->offset + blockToMerge->size == itor->offset )
            {
                blockToMerge->size += itor->size;
                size_t idx = blockToMerge - blocks.begin();
                efficientVectorRemove( blocks, itor );

                mergeContiguousBlocks( blocks.begin() + idx, blocks );
                return;
            }

            ++itor;
        }
    }
    //-----------------------------------------------------------------------------------
    VertexBufferPacked* GL3PlusVaoManager::createVertexBufferImpl( size_t numElements,
                                                                   uint32 bytesPerElement,
                                                                   BufferType bufferType,
                                                                   void *initialData, bool keepAsShadow,
                                                                   const VertexElement2Vec &vElements )
    {
        size_t vboIdx;
        size_t bufferOffset;

        allocateVbo( numElements * bytesPerElement, bytesPerElement, bufferType, vboIdx, bufferOffset );

        return 0;
    }
    //-----------------------------------------------------------------------------------
    const GLStagingBuffer& GL3PlusVaoManager::getStagingBuffer( size_t sizeBytes, bool forUpload )
    {
        /*GLStagingBufferVec::iterator itor = mStagingBuffers[forUpload].begin();
        GLStagingBufferVec::iterator end  = mStagingBuffers[forUpload].end();

        size_t bestBufferIdx = 0;

        while( itor != end )
        {
            const GLStagingBuffer &bestBuffer = mStagingBuffers[forUpload][bestBufferIdx];
            GLStagingBuffer &stagingBuffer = *itor;
            if( stagingBuffer.bufferSize <= bestBuffer.bufferSize &&
                stagingBuffer.framesSinceLastUse >= mMinStagingFrameLatency[forUpload] &&
                ( stagingBuffer.framesSinceLastUse >= bestBuffer.framesSinceLastUse ||
                  stagingBuffer.framesSinceLastUse >= mMaxStagingFrameLatency[forUpload] ) )
            {
                bestBufferIdx = itor - mStagingBuffers[forUpload].begin();
            }

            ++itor;
        }

        if( mStagingBuffers[forUpload].empty() ||
            mStagingBuffers[forUpload][bestBufferIdx].framesSinceLastUse <
                                        mMinStagingFrameLatency[forUpload] )
        {
            GLStagingBuffer newStageBuffer( sizeBytes, forUpload ? GL_COPY_READ_BUFFER :
                                                                   GL_COPY_WRITE_BUFFER );

            OCGLE( glGenBuffers( 1, &newStageBuffer.bufferName ) );
            OCGLE( glBindBuffer( newStageBuffer.target, newStageBuffer.bufferName ) );

            //TODO: Check out of memory errors.
            if( mArbBufferStorage )
            {
                OCGLE( glBufferStorage( newStageBuffer.target, sizeBytes, 0,
                                        forUpload ? GL_MAP_WRITE_BIT : GL_MAP_READ_BIT ) );
            }
            else
            {
                OCGLE( glBufferData( newStageBuffer.target, sizeBytes, 0, GL_STREAM_COPY ) );
            }

            bestBufferIdx = mStagingBuffers[forUpload].size();
            mStagingBuffers[forUpload].push_back( newStageBuffer );

            return mStagingBuffers[forUpload].back();
        }
        else
        {
            GLStagingBuffer &retVal = mStagingBuffers[forUpload][bestBufferIdx];
            retVal.framesSinceLastUse = 0;
            OCGLE( glBindBuffer( retVal.target, retVal.bufferName ) );

            return retVal;
        }*/
    }
    //-----------------------------------------------------------------------------------
    /*IndexBufferPacked* GL3PlusVaoManager::createIndexBufferImpl( IndexBufferPacked::IndexType indexType,
                                                      size_t numIndices, BufferType bufferType,
                                                      void *initialData, bool keepAsShadow )
    {
        return createIndexBufferImpl( 0, numIndices, indexType == IndexBufferPacked::IT_16BIT ? 2 : 4,
                                      bufferType, 0, initialData, keepAsShadow );
    }*/
}
