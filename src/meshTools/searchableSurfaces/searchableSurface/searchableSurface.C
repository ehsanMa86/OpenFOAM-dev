/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     | Website:  https://openfoam.org
    \\  /    A nd           | Copyright (C) 2011-2025 OpenFOAM Foundation
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

\*---------------------------------------------------------------------------*/

#include "searchableSurface.H"
#include "Time.H"
#include "triSurface.H"
#include "OSspecific.H"

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

namespace Foam
{
    defineTypeNameAndDebug(searchableSurface, 0);
    defineRunTimeSelectionTable(searchableSurface, dictionary);
}

Foam::word Foam::searchableSurface::geometryDir_("geometry");


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

Foam::autoPtr<Foam::searchableSurface> Foam::searchableSurface::New
(
    const word& searchableSurfaceType,
    const IOobject& io,
    const dictionary& dict
)
{
    dictionaryConstructorTable::iterator cstrIter =
        dictionaryConstructorTablePtr_->find(searchableSurfaceType);

    if (cstrIter == dictionaryConstructorTablePtr_->end())
    {
        FatalErrorInFunction
            << "Unknown searchableSurface type " << searchableSurfaceType
            << endl << endl
            << "Valid searchableSurface types : " << endl
            << dictionaryConstructorTablePtr_->sortedToc()
            << exit(FatalError);
    }

    return autoPtr<searchableSurface>(cstrIter()(io, dict));
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::searchableSurface::searchableSurface(const IOobject& io)
:
    regIOobject(io)
{}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

Foam::searchableSurface::~searchableSurface()
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

const Foam::word& Foam::searchableSurface::geometryDir()
{
    return geometryDir_;
}


const Foam::word& Foam::searchableSurface::geometryDir(const Time& time)
{
    if
    (
        isDir
        (
            time.rootPath()/time.globalCaseName()/time.constant()/
            searchableSurface::geometryDir_
        )
    )
    {
        return searchableSurface::geometryDir_;
    }
    else if
    (
        isDir
        (
            time.rootPath()/time.globalCaseName()/time.constant()/
            triSurface::typeName
        )
    )
    {
        return triSurface::typeName;
    }
    else
    {
        return searchableSurface::geometryDir_;
    }
}


void Foam::searchableSurface::findNearest
(
    const pointField& sample,
    const scalarField& nearestDistSqr,
    List<pointIndexHit>& info,
    vectorField& normal,
    labelList& region
) const
{
    findNearest(sample, nearestDistSqr, info);
    getNormal(info, normal);
    getRegion(info, region);
}


// ************************************************************************* //
