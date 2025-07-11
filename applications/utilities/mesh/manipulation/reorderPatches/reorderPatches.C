/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     | Website:  https://openfoam.org
    \\  /    A nd           | Copyright (C) 2024-2025 OpenFOAM Foundation
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

Application
    reorderPatches

Description
    Utility to reorder the patches of a case

    The new patch order may be specified directly as a list of patch names
    following the -patchOrder option or from the boundary file of a reference
    case specified using the -referenceCase option with or without the
    -referenceMesh or -referenceRegion options.

    This utility run either serial or parallel but either way the reference
    case boundary file is read from the constant directory.

Usage
    \b reorderPatches

    Options:
      - \par -patchOrder \<patch names\>
        Specify the list of patch names in the new order.

      - \par -referenceCase \<case path\>
        Specify the reference case path, if not specified the current case
        is used.

      - \par -referenceMesh \<name\>
        Specify the mesh in the meshes directory of the reference case.

      - \par -referenceRegion \<name\>
        Specify the mesh region for the reference case or reference case mesh

      - \par -noOverwrite \n
        Do not replace the old mesh with the new one, writing the new one
        into a separate time directory

      - \par -region \<name\>
        Specify an alternative mesh region.

\*---------------------------------------------------------------------------*/

#include "argList.H"
#include "fvMesh.H"
#include "polyBoundaryMeshEntries.H"
#include "timeSelector.H"

using namespace Foam;

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

int main(int argc, char *argv[])
{
    timeSelector::addOptions(true, false);

    argList::addNote
    (
        "Utility to reorder the patches of a case.\n"
    );

    #include "addNoOverwriteOption.H"
    #include "addMeshOption.H"
    #include "addRegionOption.H"

    argList::addOption
    (
        "patchOrder",
        "wordList",
        "specify the list of patch names in the new order"
    );

    argList::addOption
    (
        "referenceCase",
        "fileName",
        "specify the reference case path"
    );

    argList::addOption
    (
        "referenceMesh",
        "word",
        "specify the reference case mesh in the meshes directory"
    );

    argList::addOption
    (
        "referenceRegion",
        "word",
        "specify the reference mesh region"
    );

    #include "setRootCase.H"

    wordList referencePatchNames;

    if (args.optionFound("patchOrder"))
    {
        args.optionLookup("patchOrder")() >> referencePatchNames;
    }
    else if
    (
        args.optionFound("referenceCase")
     || args.optionFound("referenceMesh")
     || args.optionFound("referenceRegion")
    )
    {
        const fileName referenceCasePath
        (
            args.optionLookupOrDefault<fileName>("referenceCase", args.path())
        );
        const fileName rootDirReference = referenceCasePath.path().toAbsolute();
        const fileName caseDirReference = referenceCasePath.name();

        const string caseDirOrig = getEnv("FOAM_CASE");
        const string caseNameOrig = getEnv("FOAM_CASENAME");
        setEnv("FOAM_CASE", rootDirReference/caseDirReference, true);
        setEnv("FOAM_CASENAME", caseDirReference, true);
        Time referenceRunTime
        (
            Time::controlDictName,
            rootDirReference,
            caseDirReference,
            false
        );
        setEnv("FOAM_CASE", caseDirOrig, true);
        setEnv("FOAM_CASENAME", caseNameOrig, true);

        #include "setMeshPath.H"

        // Initialise the reference mesh path to the current mesh path
        fileName referenceMeshDir(meshPath);

        // Optionally set the reference mesh path to that specified
        if (args.optionFound("referenceMesh"))
        {
            referenceMeshDir = "meshes"/args["referenceMesh"];
        }

        // Add the optional region to the reference mesh path
        if (args.optionFound("referenceRegion"))
        {
            referenceMeshDir /= args["referenceRegion"];
        }

        // Add polyMesh to the reference mesh path
        referenceMeshDir /= polyMesh::meshSubDir;

        typeIOobject<polyBoundaryMesh> ioObj
        (
            "boundary",
            referenceRunTime.constant(),
            referenceMeshDir,
            referenceRunTime,
            IOobject::MUST_READ,
            IOobject::NO_WRITE,
            false
        );

        if (ioObj.headerOk())
        {
            polyBoundaryMeshEntries patchEntries(ioObj);

            referencePatchNames.setSize(patchEntries.size());

            forAll(patchEntries, patchi)
            {
                referencePatchNames[patchi] = patchEntries[patchi].keyword();
            }
        }
        else
        {
            FatalErrorInFunction
                << "Cannot find or read the boundary file for reference mesh "
                << nl
                << "    " << referenceCasePath/referenceMeshDir
                << exit(FatalError);
        }
    }
    else
    {
        FatalErrorInFunction
            << "Reference patch order not specified"
            << exit(FatalError);
    }

    #include "createTimeNoFunctionObjects.H"

    // Select time if specified
    timeSelector::selectIfPresent(runTime, args);

    #include "createSpecifiedMeshNoChangers.H"

    #include "setNoOverwrite.H"

    const word oldInstance = mesh.pointsInstance();

    if (!overwrite)
    {
        runTime++;
    }

    if
    (
        !Pstream::parRun()
     && mesh.boundaryMesh().size() != referencePatchNames.size()
    )
    {
        FatalErrorInFunction
            << "Number of reference patches " << referencePatchNames.size()
            << " is not equal to the number of patches in the mesh "
            << mesh.boundaryMesh().size()
            << exit(FatalError);
    }

    // Reorder the patches
    labelList newToOldPatches(identityMap(mesh.boundaryMesh().size()));

    forAll(referencePatchNames, patchi)
    {
        const label oldPatchi =
            mesh.boundaryMesh().findIndex(referencePatchNames[patchi]);

        if (oldPatchi != -1)
        {
            newToOldPatches[patchi] = oldPatchi;
        }
        else
        {
            FatalErrorInFunction
                << "Cannot find reference patch " << referencePatchNames[patchi]
                << " in the mesh which has patches "
                << mesh.boundaryMesh().names()
                << exit(FatalError);
        }
    }
    mesh.reorderPatches(newToOldPatches, false);

    if (overwrite)
    {
        mesh.setInstance(oldInstance);
    }

    Info<< "Writing mesh to " << mesh.facesInstance() << endl;
    mesh.write();

    Info<< "End\n" << endl;

    return 0;
}


// ************************************************************************* //
