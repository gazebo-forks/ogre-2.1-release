/*
-----------------------------------------------------------------------------
This source file is part of OGRE
    (Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-2013 Torus Knot Software Ltd

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

#include "OgreStableHeaders.h"

#include "Math/Array/OgreArrayConfig.h"
#include "Math/Array/OgreArrayMemoryManager.h"

namespace Ogre
{
	ArrayMemoryManager::ArrayMemoryManager( ManagerType managerType, size_t const *elementsMemSize,
											size_t numElementsSize, uint16 depthLevel,
											size_t hintMaxNodes, size_t cleanupThreshold,
											size_t maxHardLimit, RebaseListener *rebaseListener ) :
							m_elementsMemSizes( elementsMemSize ),
							m_usedMemory( 0 ),
							m_maxMemory( hintMaxNodes ),
							m_maxHardLimit( maxHardLimit ),
							m_cleanupThreshold( cleanupThreshold ),
							m_rebaseListener( rebaseListener ),
							m_level( depthLevel ),
							m_managerType( managerType )
	{
		m_memoryPools.resize( numElementsSize, 0 );
		for( size_t i=0; i<numElementsSize; ++i )
			m_totalMemoryMultiplier += m_elementsMemSizes[i];

		//If m_maxMemory == 1, it cannot grow because 1/2 truncates to 0
		m_maxMemory = std::max<size_t>( 2, m_maxMemory );

		// Round up max memory & hard limit to the next multiple of ARRAY_PACKED_REALS
		m_maxMemory   += (ARRAY_PACKED_REALS - m_maxMemory % ARRAY_PACKED_REALS) % ARRAY_PACKED_REALS;
		m_maxHardLimit+= (ARRAY_PACKED_REALS - m_maxHardLimit % ARRAY_PACKED_REALS) % ARRAY_PACKED_REALS;

		if( !m_rebaseListener )
		{
			//If there's no listener to rebase, we can't later grow the memory pool or perform cleanups.
			m_maxHardLimit		= m_maxMemory;
			m_cleanupThreshold	= -1;
		}

		size_t i=0;
		MemoryPoolVec::iterator itor = m_memoryPools.begin();
		MemoryPoolVec::iterator end  = m_memoryPools.end();

		while( itor != end )
		{
//			*itor = reinterpret_cast<char*>( _aligned_malloc( m_maxMemory * ElementsMemSize[i], 16 ) );
			*itor = (char*)OGRE_MALLOC_SIMD( m_maxMemory * m_elementsMemSizes[i],
											 MEMCATEGORY_SCENE_OBJECTS );
			memset( *itor, 0, m_maxMemory * m_elementsMemSizes[i] );
			++i;
			++itor;
		}
	}
	//-----------------------------------------------------------------------------------
	ArrayMemoryManager::~ArrayMemoryManager()
	{
		MemoryPoolVec::iterator itor = m_memoryPools.begin();
		MemoryPoolVec::iterator end  = m_memoryPools.end();

		while( itor != end )
		{
			OGRE_FREE_SIMD( *itor, MEMCATEGORY_SCENE_OBJECTS );
			*itor++ = 0;
		}
	}
	//-----------------------------------------------------------------------------------
	size_t ArrayMemoryManager::getFreeMemory() const
	{
		return ( m_maxMemory - m_usedMemory + m_availableSlots.size() ) * m_totalMemoryMultiplier;
	}
	//-----------------------------------------------------------------------------------
	size_t ArrayMemoryManager::getUsedMemory() const
	{
		return ( m_usedMemory - m_availableSlots.size() ) * m_totalMemoryMultiplier;
	}
	//-----------------------------------------------------------------------------------
	size_t ArrayMemoryManager::getWastedMemory() const
	{
		return m_availableSlots.size() * m_totalMemoryMultiplier;
	}
	//-----------------------------------------------------------------------------------
	size_t ArrayMemoryManager::getAllMemory() const
	{
		return m_maxMemory * m_totalMemoryMultiplier;
	}
	//-----------------------------------------------------------------------------------
	size_t ArrayMemoryManager::createNewSlot()
	{
		size_t nextSlot = m_usedMemory;
		++m_usedMemory;

		//See if we can reuse a slot that was previously acquired and released
		if( !m_availableSlots.empty() )
		{
			nextSlot = m_availableSlots.back();
			m_availableSlots.pop_back();
			--m_usedMemory;
		}

		if( m_usedMemory >= m_maxMemory )
		{
			if( m_maxMemory >= m_maxHardLimit )
			{
				OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS,
							"Trying to allocate more memory than the limit allowed by user",
							"ArrayMemoryManager::createNewNode" );
			}

			//Build the diff list for rebase later.
			PtrdiffVec diffsList;
			diffsList.reserve( m_usedMemory );
			m_rebaseListener->buildDiffList( m_managerType, m_level, m_memoryPools, diffsList );

			//Reallocate, grow by 50% increments, rounding up to next multiple of ARRAY_PACKED_REALS
			size_t newMemory = std::min( m_maxMemory + (m_maxMemory >> 1), m_maxHardLimit );
			newMemory+= (ARRAY_PACKED_REALS - newMemory % ARRAY_PACKED_REALS) % ARRAY_PACKED_REALS;
			newMemory = std::min( newMemory, m_maxHardLimit );

			size_t i=0;
			MemoryPoolVec::iterator itor = m_memoryPools.begin();
			MemoryPoolVec::iterator end  = m_memoryPools.end();

			while( itor != end )
			{
				//Reallocate
				char *tmp = (char*)OGRE_MALLOC_SIMD( newMemory * m_elementsMemSizes[i],
													 MEMCATEGORY_SCENE_OBJECTS );
				memcpy( tmp, *itor, m_maxMemory * m_elementsMemSizes[i] );
				memset( tmp + m_maxMemory * m_elementsMemSizes[i], 0,
						(newMemory - m_maxMemory) * m_elementsMemSizes[i] );
				OGRE_FREE_SIMD( *itor, MEMCATEGORY_SCENE_OBJECTS );
				*itor = tmp;
				++i;
				++itor;
			}

			m_maxMemory = newMemory;

			//Rebase all ptrs
			m_rebaseListener->applyRebase( m_managerType, m_level, m_memoryPools, diffsList );
		}

		return nextSlot;
	}
	//-----------------------------------------------------------------------------------
	void ArrayMemoryManager::destroySlot( const char *ptrToFirstElement, uint8 index )
	{
		const char *basePtr	= ptrToFirstElement;

		const size_t slot = ( basePtr - m_memoryPools[0] ) / m_elementsMemSizes[0] + index;

		assert( slot < m_maxMemory && "This slot does not belong to this ArrayMemoryManager" );

		if( slot + 1 == m_usedMemory )
		{
			//Lucky us, LIFO. We're done.
			--m_usedMemory;
		}
		else
		{
			//Not so lucky, add to "reuse" pool
			m_availableSlots.push_back( slot );

			//The pool is getting to big? Do some cleanup (depending
			//on fragmentation, may take a performance hit)
			if( m_availableSlots.size() > m_cleanupThreshold )
			{
				//Sort, last values first. This may improve performance in some
				//scenarios by reducing the amount of data to be shifted
				std::sort( m_availableSlots.begin(), m_availableSlots.end(), std::greater<size_t>() );
				SlotsVec::const_iterator itor = m_availableSlots.begin();
				SlotsVec::const_iterator end  = m_availableSlots.end();

				while( itor != end )
				{
					//First see if we have a continuous range of unused slots
					size_t lastRange = 1;
					SlotsVec::const_iterator it = m_availableSlots.begin() + 1;
					while( it != end && (*itor - lastRange) == *it )
					{
						++lastRange;
						++it;
					}

					size_t i=0;
					MemoryPoolVec::iterator itPools = m_memoryPools.begin();
					MemoryPoolVec::iterator enPools = m_memoryPools.end();

					//Shift everything N slots (N = lastRange)
					while( itPools != enPools )
					{
						memcpy( *itPools + ( (*itor - lastRange + 1) * m_elementsMemSizes[i] ),
								*itPools + ( (*itor + 1) * m_elementsMemSizes[i] ),
								( m_usedMemory - *itor - 1 ) * m_elementsMemSizes[i] );
						++i;
						++itPools;
					}

					m_rebaseListener->performCleanup( m_managerType, m_level, m_memoryPools,
														m_elementsMemSizes, *itor, lastRange );

					m_usedMemory -= lastRange;
					itor += lastRange;
				}

				m_availableSlots.clear();
			}
		}
	}
}