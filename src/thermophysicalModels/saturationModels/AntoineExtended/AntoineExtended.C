/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     | Website:  https://openfoam.org
    \\  /    A nd           | Copyright (C) 2015-2025 OpenFOAM Foundation
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

#include "AntoineExtended.H"
#include "addToRunTimeSelectionTable.H"

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

namespace Foam
{
namespace saturationModels
{
    defineTypeNameAndDebug(AntoineExtended, 0);
    addToRunTimeSelectionTable
    (
        saturationPressureModel,
        AntoineExtended,
        dictionary
    );

    static const coefficient oneT(dimTemperature, 1);
}
}


// * * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * * //

template<class FieldType>
Foam::tmp<FieldType>
Foam::saturationModels::AntoineExtended::pSat(const FieldType& T) const
{
    return onePbyTpowD_*exp(A_ + B_/(C_ + T) + E_*pow(T, F_))*pow(T, D_);
}


template<class FieldType>
Foam::tmp<FieldType>
Foam::saturationModels::AntoineExtended::pSatPrime(const FieldType& T) const
{
    return pSat(T)*((D_ + E_*(F_*pow(T, F_)))/T - B_/sqr(C_ + T));
}


template<class FieldType>
Foam::tmp<FieldType>
Foam::saturationModels::AntoineExtended::lnPSat(const FieldType& T) const
{
    return A_ + B_/(C_ + T) + D_*log(T/oneT) + E_*pow(T, F_);
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::saturationModels::AntoineExtended::AntoineExtended
(
    const dictionary& dict
)
:
    saturationPressureModel(),
    A_("A", dimless, dict),
    B_("B", dimTemperature, dict),
    C_("C", dimTemperature, dict),
    D_("D", dimless, dict),
    F_("F", dimless, dict),
    E_("E", dimless/pow(dimTemperature, F_), dict),
    onePbyTpowD_(dimPressure/pow(dimTemperature, D_), 1)
{}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

Foam::saturationModels::AntoineExtended::~AntoineExtended()
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

IMPLEMENT_PSAT(saturationModels::AntoineExtended, scalarField);


IMPLEMENT_PSAT(saturationModels::AntoineExtended, volScalarField::Internal);


IMPLEMENT_PSAT(saturationModels::AntoineExtended, volScalarField);


// ************************************************************************* //
