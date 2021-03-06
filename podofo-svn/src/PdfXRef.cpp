/***************************************************************************
 *   Copyright (C) 2007 by Dominik Seichter                                *
 *   domseichter@web.de                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include "PdfXRef.h"

#include "PdfOutputDevice.h"

#include <algorithm>

namespace PoDoFo {


bool PdfXRef::PdfXRefBlock::InsertItem( const TXRefItem & rItem, bool bUsed )
{
    if( rItem.reference.ObjectNumber() == m_nFirst + m_nCount ) 
    {
        // Insert at back
        m_nCount++;

        if( bUsed ) 
            items.push_back( rItem );
        else
            freeItems.push_back( rItem.reference );

        return true; // no sorting required
    }
    else if( rItem.reference.ObjectNumber() ==  m_nFirst - 1 )
    {
        // Insert at front 
        m_nFirst--;
        m_nCount++;
        
        // This is known to be slow, but should not occur actually
        if( bUsed ) 
            items.insert( items.begin(), rItem );
        else
            freeItems.insert( freeItems.begin(), rItem.reference );

        return true; // no sorting required
    }
    else if( rItem.reference.ObjectNumber() > m_nFirst - 1 &&
             rItem.reference.ObjectNumber() < m_nFirst + m_nCount ) 
    {
        // Insert at back
        m_nCount++;

        if( bUsed ) 
        {
            items.push_back( rItem );
            std::sort( items.begin(), items.end() );
        }
        else
        {
            freeItems.push_back( rItem.reference );
            std::sort( freeItems.begin(), freeItems.end() );
        }

        return true;
    }

    return false;
}

PdfXRef::PdfXRef() 
{

}

PdfXRef::~PdfXRef() 
{

}

void PdfXRef::AddObject( const PdfReference & rRef, pdf_long lOffset, bool bUsed )
{
    TIVecXRefBlock     it = m_vecBlocks.begin();
    PdfXRef::TXRefItem item( rRef, lOffset );
    bool               bInsertDone = false;

    while( it != m_vecBlocks.end() )
    {
        if( (*it).InsertItem( item, bUsed ) )
        {
            bInsertDone = true;
            break;
        }

        ++it;
    }

    if( !bInsertDone ) 
    {
        PdfXRefBlock block;
        block.m_nFirst = rRef.ObjectNumber();
        block.m_nCount = 1;
        if( bUsed )
            block.items.push_back( item );
        else
            block.freeItems.push_back( rRef );

        m_vecBlocks.push_back( block );
        std::sort( m_vecBlocks.begin(), m_vecBlocks.end() );
    }
}

void PdfXRef::Write( PdfOutputDevice* pDevice )
{
    PdfXRef::TCIVecXRefBlock  it         = m_vecBlocks.begin();
    PdfXRef::TCIVecXRefItems  itItems;
    PdfXRef::TCIVecReferences itFree;
    const PdfReference*       pNextFree  = NULL;

    unsigned int nFirst       = 0;
    unsigned int nCount       = 0;

    MergeBlocks();

    m_lOffset = pDevice->Tell();
    this->BeginWrite( pDevice );
    while( it != m_vecBlocks.end() )
    {
        nCount       = (*it).m_nCount;
        nFirst       = (*it).m_nFirst;
        itFree       = (*it).freeItems.begin();
        itItems      = (*it).items.begin();

        if( nFirst == 1 )
        {
            --nFirst;
            ++nCount;
        }

        // when there is only one, then we need to start with 0 and the bogus object...
        this->WriteSubSection( pDevice, nFirst, nCount );

        if( !nFirst ) 
        {
            const PdfReference* pFirstFree = this->GetFirstFreeObject( it, itFree );
            this->WriteXRefEntry( pDevice, pFirstFree ? pFirstFree->ObjectNumber() : 0, EMPTY_OBJECT_OFFSET, 'f' );
        }

        while( itItems != (*it).items.end() )
        {
            // check if there is a free object at the current position
            while( itFree != (*it).freeItems.end() &&
                   *itFree < (*itItems).reference )
            {
                int nGen  = (*itFree).GenerationNumber();

                // get a pointer to the next free object
                pNextFree = this->GetNextFreeObject( it, itFree );
                
                // write free object
                this->WriteXRefEntry( pDevice, pNextFree ? pNextFree->ObjectNumber() : 0, nGen, 'f' );
                ++itFree;
            }

            this->WriteXRefEntry( pDevice, (*itItems).lOffset, (*itItems).reference.GenerationNumber(), 'n', 
                                  (*itItems).reference.ObjectNumber()  );
            ++itItems;
        }

        // Check if there are any free objects left!
        while( itFree != (*it).freeItems.end() )
        {
            int nGen  = (*itFree).GenerationNumber();
            
            // get a pointer to the next free object
            pNextFree = this->GetNextFreeObject( it, itFree );
            
            // write free object
            this->WriteXRefEntry( pDevice, pNextFree ? pNextFree->ObjectNumber() : 0, nGen, 'f' );
            ++itFree;
        }

        ++it;
    }

    this->EndWrite( pDevice );
}

const PdfReference* PdfXRef::GetFirstFreeObject( PdfXRef::TCIVecXRefBlock itBlock, PdfXRef::TCIVecReferences itFree ) const 
{
    const PdfReference* pRef      = NULL;

    // find the next free object
    while( itBlock != m_vecBlocks.end() )
    {
        if( itFree != (*itBlock).freeItems.end() )
            break; // got a free object
        
        ++itBlock;
        if(itBlock != m_vecBlocks.end())
            itFree = (*itBlock).freeItems.begin();
    }

    // if there is another free object, return it
    if( itBlock != m_vecBlocks.end() &&
        itFree != (*itBlock).freeItems.end() )
    {
        pRef = &(*itFree);
                
        return pRef;
    }

    return pRef;
}

const PdfReference* PdfXRef::GetNextFreeObject( PdfXRef::TCIVecXRefBlock itBlock, PdfXRef::TCIVecReferences itFree ) const 
{
    const PdfReference* pRef      = NULL;

    // check if itFree points to a valid free object at the moment
    if( itFree != (*itBlock).freeItems.end() )
        ++itFree; // we currently have a free object, so go to the next one

    // find the next free object
    while( itBlock != m_vecBlocks.end() )
    {
        if( itFree != (*itBlock).freeItems.end() )
            break; // got a free object
        
        ++itBlock;
        if( itBlock != m_vecBlocks.end() )
            itFree = (*itBlock).freeItems.begin();
    }

    // if there is another free object, return it
    if( itBlock != m_vecBlocks.end() &&
        itFree != (*itBlock).freeItems.end() )
    {
        pRef = &(*itFree);
                
        return pRef;
    }

    return pRef;
}

unsigned int PdfXRef::GetSize() const
{

    unsigned int                       nCount = 0;
    PdfXRef::TCIVecXRefBlock  it     = m_vecBlocks.begin();

    while( it != m_vecBlocks.end() )
    {
        nCount += (*it).m_nCount;
        ++it;
    }
    
    //return nCount;
    if( !m_vecBlocks.size() )
        return 0;

    const PdfXRefBlock& lastBlock = m_vecBlocks.back();
    unsigned int highObj  = lastBlock.items.size() ? lastBlock.items.back().reference.ObjectNumber() : 0;
    unsigned int highFree = lastBlock.freeItems.size() ? lastBlock.freeItems.back().ObjectNumber() : 0;

    unsigned int max      =  PDF_MAX( highObj, highFree );

    // From the PdfReference: /Size's value is 1 greater than the highes object number used in the file.
    return max+1;
}

void PdfXRef::MergeBlocks() 
{
    PdfXRef::TIVecXRefBlock  it     = m_vecBlocks.begin();
    PdfXRef::TIVecXRefBlock  itNext = it+1;

    // Do not crash in case we have no blocks at all
    if( it == m_vecBlocks.end() )
    {
	PODOFO_RAISE_ERROR( ePdfError_NoXRef );
    }

    while( itNext != m_vecBlocks.end() )
    {
        if( (*itNext).m_nFirst == (*it).m_nFirst + (*it).m_nCount ) 
        {
            // merge the two 
            (*it).m_nCount += (*itNext).m_nCount;

            (*it).items.reserve( (*it).items.size() + (*itNext).items.size() );
            (*it).items.insert( (*it).items.end(), (*itNext).items.begin(), (*itNext).items.end() );

            (*it).freeItems.reserve( (*it).freeItems.size() + (*itNext).freeItems.size() );
            (*it).freeItems.insert( (*it).freeItems.end(), (*itNext).freeItems.begin(), (*itNext).freeItems.end() );

            itNext = m_vecBlocks.erase( itNext );
            it     = itNext - 1;
        }
        else
            it = itNext++;
    }
}

void PdfXRef::BeginWrite( PdfOutputDevice* pDevice ) 
{
    pDevice->Print( "xref\n" );
}

void PdfXRef::WriteSubSection( PdfOutputDevice* pDevice, unsigned int nFirst, unsigned int nCount )
{
#ifdef DEBUG
    PdfError::DebugMessage("Writing XRef section: %u %u\n", nFirst, nCount );
#endif // DEBUG
    pDevice->Print( "%u %u\n", nFirst, nCount );
}

void PdfXRef::WriteXRefEntry( PdfOutputDevice* pDevice, size_t lOffset, 
                              unsigned long lGeneration, char cMode, unsigned long ) 
{
    pDevice->Print( "%0.10i %0.5i %c \n", lOffset, lGeneration, cMode );
}

void PdfXRef::EndWrite( PdfOutputDevice* ) 
{
}

};
