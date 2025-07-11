/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     | Website:  https://openfoam.org
    \\  /    A nd           | Copyright (C) 2012-2025 OpenFOAM Foundation
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
    Foam::fv::fixedTemperature

Description
    Fixed temperature equation constraint

Usage
    \verbatim
    fixedTemperature
    {
        type            fixedTemperature;

        cellZone        all;

        phase           gas; // Optional phase name

        // Uniform temperature constraint
        mode            uniform;
        temperature     constant 500; // Uniform temperature

        // // Looked-up field temperature constraint
        // T            T; // Temperature field name
    }
    \endverbatim

    Note:
        The 'uniform' option allows the use of a time-varying uniform
        temperature by means of the Function1 type.

SourceFiles
    fixedTemperature.C

\*---------------------------------------------------------------------------*/

#ifndef fixedTemperature_H
#define fixedTemperature_H

#include "fvConstraint.H"
#include "fvCellZone.H"
#include "NamedEnum.H"
#include "Function1.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace fv
{

/*---------------------------------------------------------------------------*\
                 Class fixedTemperature Declaration
\*---------------------------------------------------------------------------*/

class fixedTemperature
:
    public fvConstraint
{
public:

    //- Temperature mode
    enum class temperatureMode
    {
        uniform,
        lookup
    };

    //- String representation of mode enums
    static const NamedEnum<temperatureMode, 2> modeNames_;


private:

    // Private Data

        //- The cellZone the fvConstraint applies to
        fvCellZone zone_;

        //- Operation mode
        temperatureMode mode_;

        //- Uniform temperature [K]
        autoPtr<Function1<scalar>> TValue_;

        //- Temperature field name
        word TName_;

        //- Optional phase name
        word phaseName_;

        //- Fraction of the constraint to apply. Facilitates ramping, or
        //  pulsing, or deactivation after a time. Should take a value between
        //  0 and 1. Defaults to 1 (i.e., apply all of the constraint).
        autoPtr<Function1<scalar>> fraction_;


    // Private Member Functions

        //- Non-virtual read
        void readCoeffs(const dictionary& dict);


public:

    //- Runtime type information
    TypeName("fixedTemperature");


    // Constructors

        //- Construct from components
        fixedTemperature
        (
            const word& name,
            const word& modelType,
            const fvMesh& mesh,
            const dictionary& dict
        );

        //- Disallow default bitwise copy construction
        fixedTemperature(const fixedTemperature&) = delete;


    //- Destructor
    virtual ~fixedTemperature()
    {}


    // Member Functions

        //- Return the list of fields constrained by the fvConstraint
        virtual wordList constrainedFields() const;

        //- Constrain energy equation to fix the temperature
        virtual bool constrain
        (
            fvMatrix<scalar>& eqn,
            const word& fieldName
        ) const;

        //- Update for mesh motion
        virtual bool movePoints();

        //- Update topology using the given map
        virtual void topoChange(const polyTopoChangeMap&);

        //- Update from another mesh using the given map
        virtual void mapMesh(const polyMeshMap&);

        //- Redistribute or update using the given distribution map
        virtual void distribute(const polyDistributionMap&);

        //- Read dictionary
        virtual bool read(const dictionary& dict);


    // Member Operators

        //- Disallow default bitwise assignment
        void operator=(const fixedTemperature&) = delete;
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace fv
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
