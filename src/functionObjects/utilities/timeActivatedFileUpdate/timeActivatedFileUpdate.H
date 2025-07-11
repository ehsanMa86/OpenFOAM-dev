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

Class
    Foam::functionObjects::timeActivatedFileUpdate

Description
    Performs a file copy/replacement once a specified time has been reached.

Usage
    Example usage to update the fvSolution dictionary at 0.1, 0.2 and 0.3s
    during the run:
    \verbatim
    fileUpdate1
    {
        type            timeActivatedFileUpdate;

        libs            ("libutilityFunctionObjects.so");

        writeControl    timeStep;
        writeInterval   1;
        fileToUpdate    "$FOAM_CASE/system/fvSolution";
        fileVsTime
        (
            (-1 "$FOAM_CASE/system/fvSolution.0")
            (0.10 "$FOAM_CASE/system/fvSolution.10")
            (0.20 "$FOAM_CASE/system/fvSolution.20")
            (0.35 "$FOAM_CASE/system/fvSolution.35")
        );
    }
    \endverbatim

SourceFiles
    timeActivatedFileUpdate.C

\*---------------------------------------------------------------------------*/

#ifndef timeActivatedFileUpdate_functionObject_H
#define timeActivatedFileUpdate_functionObject_H

#include "functionObject.H"
#include "Tuple2.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

// Forward declaration of classes
class Time;

namespace functionObjects
{

/*---------------------------------------------------------------------------*\
                   Class timeActivatedFileUpdate Declaration
\*---------------------------------------------------------------------------*/

class timeActivatedFileUpdate
:
    public functionObject
{
    // Private Data

        //- Name of file to update
        fileName fileToUpdate_;

        //- List of filenames vs time
        List<Tuple2<scalar, fileName>> fileVsTime_;

        //- Index of last file copied
        label lastIndex_;


    // Private Member Functions

        //- Update file
        void updateFile();

        //- Disallow default bitwise copy construction
        timeActivatedFileUpdate(const timeActivatedFileUpdate&) = delete;

        //- Disallow default bitwise assignment
        void operator=(const timeActivatedFileUpdate&) = delete;


public:

    //- Runtime type information
    TypeName("timeActivatedFileUpdate");


    // Constructors

        //- Construct from Time and dictionary
        timeActivatedFileUpdate
        (
            const word& name,
            const Time& runTime,
            const dictionary& dict
        );


    //- Destructor
    virtual ~timeActivatedFileUpdate();


    // Member Functions

        //- Read the timeActivatedFileUpdate data
        virtual bool read(const dictionary&);

        //- Return the list of fields required
        virtual wordList fields() const
        {
            return wordList::null();
        }

        //- Execute file updates
        virtual bool execute();

        //- Do nothing
        virtual bool write();
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace functionObjects
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
