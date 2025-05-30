/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     | Website:  https://openfoam.org
    \\  /    A nd           | Copyright (C) 2014-2025 OpenFOAM Foundation
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
    Foam::wallLubricationModels::Frank

Description
    Wall lubrication model of Frank.

    References:
    \verbatim
        Otromke, M. (2013).
        Implementation and Comparison of Correlations for interfacial Forces in
        a Gas-Liquid System within an Euler-Euler Framework.
        PhD Thesis.
    \endverbatim

    \verbatim
        Frank, T. (2005, April).
        Advances in computational fluid dynamics (CFD) of 3-dimensional
        gas-liquid multiphase flows.
        In NAFEMS Seminar: Simulation of Complex Flows (CFD)-Applications and
        Trends, Wiesbaden, Germany (pp. 1-18).
    \endverbatim

SourceFiles
    Frank.C

\*---------------------------------------------------------------------------*/

#ifndef Frank_H
#define Frank_H

#include "dispersedWallLubricationModel.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace wallLubricationModels
{

/*---------------------------------------------------------------------------*\
                            Class Frank Declaration
\*---------------------------------------------------------------------------*/

class Frank
:
    public dispersedWallLubricationModel
{
    // Private Data

        //- Coefficient d
        const dimensionedScalar Cwd_;

        //- Coefficient c
        const dimensionedScalar Cwc_;

        //- Power p
        const scalar p_;


public:

    //- Runtime type information
    TypeName("Frank");


    // Constructors

        //- Construct from a dictionary and an interface
        Frank
        (
            const dictionary& dict,
            const phaseInterface& interface
        );


    //- Destructor
    virtual ~Frank();


    // Member Functions

        //- Return phase-intensive wall lubrication force. Note that this is
        //  signed relative to dispersed-continuous, not phase1-phase2.
        virtual tmp<volVectorField> Fi() const;
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace wallLubricationModels
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
