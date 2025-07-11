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

#include "polyMeshAdder.H"
#include "mapAddedPolyMesh.H"
#include "faceCoupleInfo.H"
#include "processorPolyPatch.H"
#include "Time.H"

// * * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * * //

// Get index of patch in new set of patchnames/types
Foam::label Foam::polyMeshAdder::patchIndex
(
    const polyPatch& p,
    DynamicList<word>& allPatchNames,
    DynamicList<word>& allPatchTypes
)
{
    // Find the patch name on the list.  If the patch is already there
    // and patch types match, return index
    const word& pType = p.type();
    const word& pName = p.name();

    label patchi = findIndex(allPatchNames, pName);

    if (patchi == -1)
    {
        // Patch not found. Append to the list
        allPatchNames.append(pName);
        allPatchTypes.append(pType);

        return allPatchNames.size() - 1;
    }
    else if (allPatchTypes[patchi] == pType)
    {
        // Found name and types match
        return patchi;
    }
    else
    {
        // Found the name, but type is different

        // Duplicate name is not allowed.  Create a composite name from the
        // patch name and case name
        const word& caseName = p.boundaryMesh().mesh().time().caseName();

        allPatchNames.append(pName + "_" + caseName);
        allPatchTypes.append(pType);

        Pout<< "label patchIndex(const polyPatch& p) : "
            << "Patch " << p.index() << " named "
            << pName << " in mesh " << caseName
            << " already exists, but patch types"
            << " do not match.\nCreating a composite name as "
            << allPatchNames.last() << endl;

        return allPatchNames.size() - 1;
    }
}


// Get index of zone in new set of zone names
Foam::label Foam::polyMeshAdder::zoneIndex
(
    const word& curName,
    DynamicList<word>& names
)
{
    label zi = findIndex(names, curName);

    if (zi != -1)
    {
        return zi;
    }
    else
    {
        // Not found.  Add new name to the list
        names.append(curName);

        return names.size() - 1;
    }
}


void Foam::polyMeshAdder::mergePatchNames
(
    const polyBoundaryMesh& patches0,
    const polyBoundaryMesh& patches1,

    DynamicList<word>& allPatchNames,
    DynamicList<word>& allPatchTypes,

    labelList& from1ToAllPatches,
    labelList& fromAllTo1Patches
)
{
    // Insert the mesh0 patches and zones
    allPatchNames.append(patches0.names());
    allPatchTypes.append(patches0.types());


    // Patches
    // ~~~~~~~
    // Patches from 0 are taken over as is; those from 1 get either merged
    // (if they share name and type) or appended.
    // Empty patches are filtered out much much later on.

    // Add mesh1 patches and build map both ways.
    from1ToAllPatches.setSize(patches1.size());

    forAll(patches1, patchi)
    {
        from1ToAllPatches[patchi] = patchIndex
        (
            patches1[patchi],
            allPatchNames,
            allPatchTypes
        );
    }
    allPatchTypes.shrink();
    allPatchNames.shrink();

    // Invert 1 to all patch map
    fromAllTo1Patches.setSize(allPatchNames.size());
    fromAllTo1Patches = -1;

    forAll(from1ToAllPatches, i)
    {
        fromAllTo1Patches[from1ToAllPatches[i]] = i;
    }
}


Foam::labelList Foam::polyMeshAdder::getPatchStarts
(
    const polyBoundaryMesh& patches
)
{
    labelList patchStarts(patches.size());
    forAll(patches, patchi)
    {
        patchStarts[patchi] = patches[patchi].start();
    }
    return patchStarts;
}


Foam::labelList Foam::polyMeshAdder::getPatchSizes
(
    const polyBoundaryMesh& patches
)
{
    labelList patchSizes(patches.size());
    forAll(patches, patchi)
    {
        patchSizes[patchi] = patches[patchi].size();
    }
    return patchSizes;
}


Foam::labelList Foam::polyMeshAdder::getFaceOrder
(
    const cellList& cells,
    const label nInternalFaces,
    const labelList& owner,
    const labelList& neighbour
)
{
    labelList oldToNew(owner.size(), -1);

    // Leave boundary faces in order
    for (label facei = nInternalFaces; facei < owner.size(); ++facei)
    {
        oldToNew[facei] = facei;
    }

    // First unassigned face
    label newFacei = 0;

    forAll(cells, celli)
    {
        const labelList& cFaces = cells[celli];

        SortableList<label> nbr(cFaces.size());

        forAll(cFaces, i)
        {
            label facei = cFaces[i];

            label nbrCelli = neighbour[facei];

            if (nbrCelli != -1)
            {
                // Internal face. Get cell on other side.
                if (nbrCelli == celli)
                {
                    nbrCelli = owner[facei];
                }

                if (celli < nbrCelli)
                {
                    // Celli is master
                    nbr[i] = nbrCelli;
                }
                else
                {
                    // nbrCell is master. Let it handle this face.
                    nbr[i] = -1;
                }
            }
            else
            {
                // External face. Do later.
                nbr[i] = -1;
            }
        }

        nbr.sort();

        forAll(nbr, i)
        {
            if (nbr[i] != -1)
            {
                oldToNew[cFaces[nbr.indices()[i]]] = newFacei++;
            }
        }
    }


    // Check done all faces.
    forAll(oldToNew, facei)
    {
        if (oldToNew[facei] == -1)
        {
            FatalErrorInFunction
                << "Did not determine new position"
                << " for face " << facei
                << abort(FatalError);
        }
    }

    return oldToNew;
}


// Adds primitives (cells, faces, points)
// Cells:
//  - all of mesh0
//  - all of mesh1
// Faces:
//  - all uncoupled of mesh0
//  - all coupled faces
//  - all uncoupled of mesh1
// Points:
//  - all coupled
//  - all uncoupled of mesh0
//  - all uncoupled of mesh1
void Foam::polyMeshAdder::mergePrimitives
(
    const polyMesh& mesh0,
    const polyMesh& mesh1,
    const faceCoupleInfo& coupleInfo,

    const label nAllPatches,                // number of patches in the new mesh
    const labelList& fromAllTo1Patches,
    const labelList& from1ToAllPatches,

    pointField& allPoints,
    labelList& from0ToAllPoints,
    labelList& from1ToAllPoints,

    faceList& allFaces,
    labelList& allOwner,
    labelList& allNeighbour,
    label& nInternalFaces,
    labelList& nFacesPerPatch,
    label& nCells,

    labelList& from0ToAllFaces,
    labelList& from1ToAllFaces,
    labelList& from1ToAllCells
)
{
    const polyBoundaryMesh& patches0 = mesh0.boundaryMesh();
    const polyBoundaryMesh& patches1 = mesh1.boundaryMesh();

    const indirectPrimitivePatch& masterPatch = coupleInfo.masterPatch();
    const indirectPrimitivePatch& slavePatch = coupleInfo.slavePatch();


    // Points
    // ~~~~~~

    // Storage for new points
    allPoints.setSize(mesh0.nPoints() + mesh1.nPoints());
    label allPointi = 0;

    from0ToAllPoints.setSize(mesh0.nPoints());
    from0ToAllPoints = -1;
    from1ToAllPoints.setSize(mesh1.nPoints());
    from1ToAllPoints = -1;

    // Copy coupled points
    {
        const labelListList coupleToMasterPoints
        (
            coupleInfo.coupleToMasterPoints()
        );
        const labelListList coupleToSlavePoints
        (
            coupleInfo.coupleToSlavePoints()
        );

        forAll(coupleToMasterPoints, couplePointi)
        {
            const labelList& masterPoints = coupleToMasterPoints[couplePointi];

            forAll(masterPoints, j)
            {
                label mesh0Pointi = masterPatch.meshPoints()[masterPoints[j]];
                from0ToAllPoints[mesh0Pointi] = allPointi;
                allPoints[allPointi] = mesh0.points()[mesh0Pointi];
            }

            const labelList& slavePoints = coupleToSlavePoints[couplePointi];

            forAll(slavePoints, j)
            {
                label mesh1Pointi = slavePatch.meshPoints()[slavePoints[j]];
                from1ToAllPoints[mesh1Pointi] = allPointi;
                allPoints[allPointi] = mesh1.points()[mesh1Pointi];
            }

            allPointi++;
        }
    }

    // Add uncoupled mesh0 points
    forAll(mesh0.points(), pointi)
    {
        if (from0ToAllPoints[pointi] == -1)
        {
            allPoints[allPointi] = mesh0.points()[pointi];
            from0ToAllPoints[pointi] = allPointi;
            allPointi++;
        }
    }

    // Add uncoupled mesh1 points
    forAll(mesh1.points(), pointi)
    {
        if (from1ToAllPoints[pointi] == -1)
        {
            allPoints[allPointi] = mesh1.points()[pointi];
            from1ToAllPoints[pointi] = allPointi;
            allPointi++;
        }
    }

    allPoints.setSize(allPointi);


    // Faces
    // ~~~~~

    // Sizes per patch
    nFacesPerPatch.setSize(nAllPatches);
    nFacesPerPatch = 0;

    // Storage for faces and owner/neighbour
    allFaces.setSize(mesh0.nFaces() + mesh1.nFaces());
    allOwner.setSize(allFaces.size());
    allOwner = -1;
    allNeighbour.setSize(allFaces.size());
    allNeighbour = -1;
    label allFacei = 0;

    from0ToAllFaces.setSize(mesh0.nFaces());
    from0ToAllFaces = -1;
    from1ToAllFaces.setSize(mesh1.nFaces());
    from1ToAllFaces = -1;

    // Copy mesh0 internal faces (always uncoupled)
    for (label facei = 0; facei < mesh0.nInternalFaces(); facei++)
    {
        allFaces[allFacei] = renumber(from0ToAllPoints, mesh0.faces()[facei]);
        allOwner[allFacei] = mesh0.faceOwner()[facei];
        allNeighbour[allFacei] = mesh0.faceNeighbour()[facei];
        from0ToAllFaces[facei] = allFacei++;
    }

    // Copy coupled faces. Every coupled face has an equivalent master and
    // slave. Also uncount as boundary faces all the newly coupled faces.
    forAll(masterPatch, coupleFacei)
    {
        const label mesh0Facei = masterPatch.addressing()[coupleFacei];

        if (from0ToAllFaces[mesh0Facei] == -1)
        {
            // First occurrence of face
            from0ToAllFaces[mesh0Facei] = allFacei;

            // External face becomes internal so uncount
            label patch0 = patches0.whichPatch(mesh0Facei);
            nFacesPerPatch[patch0]--;
        }

        const label mesh1Facei = slavePatch.addressing()[coupleFacei];

        if (from1ToAllFaces[mesh1Facei] == -1)
        {
            from1ToAllFaces[mesh1Facei] = allFacei;

            label patch1 = patches1.whichPatch(mesh1Facei);
            nFacesPerPatch[from1ToAllPatches[patch1]]--;
        }

        // Copy cut face (since cutPoints are copied first no renumbering
        // necessary)
        allFaces[allFacei] = coupleInfo.coupleFace(coupleFacei);
        allOwner[allFacei] = mesh0.faceOwner()[mesh0Facei];
        allNeighbour[allFacei] = mesh1.faceOwner()[mesh1Facei] + mesh0.nCells();

        allFacei++;
    }

    // Copy mesh1 internal faces (always uncoupled)
    for (label facei = 0; facei < mesh1.nInternalFaces(); facei++)
    {
        allFaces[allFacei] = renumber(from1ToAllPoints, mesh1.faces()[facei]);
        allOwner[allFacei] = mesh1.faceOwner()[facei] + mesh0.nCells();
        allNeighbour[allFacei] = mesh1.faceNeighbour()[facei] + mesh0.nCells();
        from1ToAllFaces[facei] = allFacei++;
    }

    nInternalFaces = allFacei;

    // Copy (unmarked/uncoupled) external faces in new order.
    for (label allPatchi = 0; allPatchi < nAllPatches; allPatchi++)
    {
        if (allPatchi < patches0.size())
        {
            // Patch is present in mesh0
            const polyPatch& pp = patches0[allPatchi];

            nFacesPerPatch[allPatchi] += pp.size();

            label facei = pp.start();

            forAll(pp, i)
            {
                if (from0ToAllFaces[facei] == -1)
                {
                    // Is uncoupled face since has not yet been dealt with
                    allFaces[allFacei] = renumber
                    (
                        from0ToAllPoints,
                        mesh0.faces()[facei]
                    );
                    allOwner[allFacei] = mesh0.faceOwner()[facei];
                    allNeighbour[allFacei] = -1;

                    from0ToAllFaces[facei] = allFacei++;
                }
                facei++;
            }
        }
        if (fromAllTo1Patches[allPatchi] != -1)
        {
            // Patch is present in mesh1
            const polyPatch& pp = patches1[fromAllTo1Patches[allPatchi]];

            nFacesPerPatch[allPatchi] += pp.size();

            label facei = pp.start();

            forAll(pp, i)
            {
                if (from1ToAllFaces[facei] == -1)
                {
                    // Is uncoupled face
                    allFaces[allFacei] = renumber
                    (
                        from1ToAllPoints,
                        mesh1.faces()[facei]
                    );
                    allOwner[allFacei] =
                        mesh1.faceOwner()[facei]
                      + mesh0.nCells();
                    allNeighbour[allFacei] = -1;

                    from1ToAllFaces[facei] = allFacei++;
                }
                facei++;
            }
        }
    }
    allFaces.setSize(allFacei);
    allOwner.setSize(allFacei);
    allNeighbour.setSize(allFacei);

    // Cells
    // ~~~~~

    from1ToAllCells.setSize(mesh1.nCells());
    from1ToAllCells = -1;

    forAll(mesh1.cells(), i)
    {
        from1ToAllCells[i] = i + mesh0.nCells();
    }

    // Make cells (= cell-face addressing)
    nCells = mesh0.nCells() + mesh1.nCells();
    cellList allCells(nCells);
    primitiveMesh::calcCells(allCells, allOwner, allNeighbour, nCells);

    // Reorder faces for upper-triangular order.
    labelList oldToNew
    (
        getFaceOrder
        (
            allCells,
            nInternalFaces,
            allOwner,
            allNeighbour
        )
    );

    inplaceReorder(oldToNew, allFaces);
    inplaceReorder(oldToNew, allOwner);
    inplaceReorder(oldToNew, allNeighbour);
    inplaceRenumber(oldToNew, from0ToAllFaces);
    inplaceRenumber(oldToNew, from1ToAllFaces);
}


void Foam::polyMeshAdder::mergePointZones
(
    const label nAllPoints,
    const pointZoneList& pz0,
    const pointZoneList& pz1,
    const labelList& from0ToAllPoints,
    const labelList& from1ToAllPoints,

    DynamicList<word>& zoneNames,
    labelList& from1ToAll,
    List<DynamicList<label>>& pzPoints
)
{
    zoneNames.setCapacity(pz0.size() + pz1.size());
    zoneNames.append(pz0.toc());

    from1ToAll.setSize(pz1.size());

    forAll(pz1, zi)
    {
        from1ToAll[zi] = zoneIndex(pz1[zi].name(), zoneNames);
    }
    zoneNames.shrink();



    // Zone(s) per point. Two levels: if only one zone
    // stored in pointToZone. Any extra stored in additionalPointToZones.
    // This is so we only allocate labelLists per point if absolutely
    // necessary.
    labelList pointToZone(nAllPoints, -1);
    labelListList addPointToZones(nAllPoints);

    // mesh0 zones kept
    forAll(pz0, zi)
    {
        const pointZone& pz = pz0[zi];

        forAll(pz, i)
        {
            label point0 = pz[i];
            label allPointi = from0ToAllPoints[point0];

            if (pointToZone[allPointi] == -1)
            {
                pointToZone[allPointi] = zi;
            }
            else if (pointToZone[allPointi] != zi)
            {
                labelList& pZones = addPointToZones[allPointi];
                if (findIndex(pZones, zi) == -1)
                {
                    pZones.append(zi);
                }
            }
        }
    }

    // mesh1 zones renumbered
    forAll(pz1, zi)
    {
        const pointZone& pz = pz1[zi];
        const label allZoneI = from1ToAll[zi];

        forAll(pz, i)
        {
            label point1 = pz[i];
            label allPointi = from1ToAllPoints[point1];

            if (pointToZone[allPointi] == -1)
            {
                pointToZone[allPointi] = allZoneI;
            }
            else if (pointToZone[allPointi] != allZoneI)
            {
                labelList& pZones = addPointToZones[allPointi];
                if (findIndex(pZones, allZoneI) == -1)
                {
                    pZones.append(allZoneI);
                }
            }
        }
    }

    // Extract back into zones

    // 1. Count
    labelList nPoints(zoneNames.size(), 0);
    forAll(pointToZone, allPointi)
    {
        label zi = pointToZone[allPointi];
        if (zi != -1)
        {
            nPoints[zi]++;
        }
    }
    forAll(addPointToZones, allPointi)
    {
        const labelList& pZones = addPointToZones[allPointi];
        forAll(pZones, i)
        {
            nPoints[pZones[i]]++;
        }
    }

    // 2. Fill
    pzPoints.setSize(zoneNames.size());
    forAll(pzPoints, zi)
    {
        pzPoints[zi].setCapacity(nPoints[zi]);
    }
    forAll(pointToZone, allPointi)
    {
        label zi = pointToZone[allPointi];
        if (zi != -1)
        {
            pzPoints[zi].append(allPointi);
        }
    }
    forAll(addPointToZones, allPointi)
    {
        const labelList& pZones = addPointToZones[allPointi];
        forAll(pZones, i)
        {
            pzPoints[pZones[i]].append(allPointi);
        }
    }
    forAll(pzPoints, i)
    {
        pzPoints[i].shrink();
        stableSort(pzPoints[i]);
    }
}


void Foam::polyMeshAdder::mergeFaceZones
(
    const labelList& allOwner,

    const polyMesh& mesh0,
    const polyMesh& mesh1,

    const labelList& from0ToAllFaces,
    const labelList& from1ToAllFaces,

    const labelList& from1ToAllCells,

    DynamicList<word>& zoneNames,
    labelList& from1ToAll,
    List<DynamicList<label>>& fzFaces,
    boolList& fzOrientations,
    List<DynamicList<bool>>& fzFlips
)
{
    const faceZoneList& fz0 = mesh0.faceZones();
    const labelList& owner0 = mesh0.faceOwner();
    const faceZoneList& fz1 = mesh1.faceZones();
    const labelList& owner1 = mesh1.faceOwner();


    zoneNames.setCapacity(fz0.size() + fz1.size());
    zoneNames.append(fz0.toc());

    from1ToAll.setSize(fz1.size());

    forAll(fz1, zi)
    {
        from1ToAll[zi] = zoneIndex(fz1[zi].name(), zoneNames);
    }
    zoneNames.shrink();

    fzOrientations.setSize(zoneNames.size(), false);

    // Zone(s) per face
    labelList faceToZone(allOwner.size(), -1);
    labelListList addFaceToZones(allOwner.size());
    boolList faceToFlip(allOwner.size(), false);
    boolListList addFaceToFlips(allOwner.size());

    // mesh0 zones kept
    forAll(fz0, zi)
    {
        const labelList& addressing = fz0[zi];

        fzOrientations[zi] = fz0[zi].oriented();

        if (fz0[zi].oriented())
        {
            const boolList& flipMap = fz0[zi].flipMap();

            forAll(addressing, i)
            {
                const label face0 = addressing[i];
                bool flip0 = flipMap[i];
                const label allFacei = from0ToAllFaces[face0];

                if (allFacei != -1)
                {
                    // Check if orientation same
                    const label allCell0 = owner0[face0];
                    if (allOwner[allFacei] != allCell0)
                    {
                        flip0 = !flip0;
                    }

                    if (faceToZone[allFacei] == -1)
                    {
                        faceToZone[allFacei] = zi;
                        faceToFlip[allFacei] = flip0;
                    }
                    else if (faceToZone[allFacei] != zi)
                    {
                        labelList& fZones = addFaceToZones[allFacei];
                        boolList& flipZones = addFaceToFlips[allFacei];

                        if (findIndex(fZones, zi) == -1)
                        {
                            fZones.append(zi);
                            flipZones.append(flip0);
                        }
                    }
                }
            }
        }
        else
        {
            forAll(addressing, i)
            {
                const label face0 = addressing[i];
                const label allFacei = from0ToAllFaces[face0];

                if (allFacei != -1)
                {
                    if (faceToZone[allFacei] == -1)
                    {
                        faceToZone[allFacei] = zi;
                    }
                    else if (faceToZone[allFacei] != zi)
                    {
                        labelList& fZones = addFaceToZones[allFacei];

                        if (findIndex(fZones, zi) == -1)
                        {
                            fZones.append(zi);
                        }
                    }
                }
            }
        }
    }

    // mesh1 zones renumbered
    forAll(fz1, zi)
    {
        const labelList& addressing = fz1[zi];
        const boolList& flipMap = fz1[zi].flipMap();

        const label allZoneI = from1ToAll[zi];

        fzOrientations[allZoneI] = fz1[zi].oriented();

        if (fz1[zi].oriented())
        {
            forAll(addressing, i)
            {
                const label face1 = addressing[i];
                bool flip1 = flipMap[i];
                const label allFacei = from1ToAllFaces[face1];

                if (allFacei != -1)
                {
                    // Check if orientation same
                    const label allCell1 = from1ToAllCells[owner1[face1]];
                    if (allOwner[allFacei] != allCell1)
                    {
                        flip1 = !flip1;
                    }

                    if (faceToZone[allFacei] == -1)
                    {
                        faceToZone[allFacei] = allZoneI;
                        faceToFlip[allFacei] = flip1;
                    }
                    else if (faceToZone[allFacei] != allZoneI)
                    {
                        labelList& fZones = addFaceToZones[allFacei];
                        boolList& flipZones = addFaceToFlips[allFacei];

                        if (findIndex(fZones, allZoneI) == -1)
                        {
                            fZones.append(allZoneI);
                            flipZones.append(flip1);
                        }
                    }
                }
            }
        }
        else
        {
            forAll(addressing, i)
            {
                const label face1 = addressing[i];
                const label allFacei = from1ToAllFaces[face1];

                if (allFacei != -1)
                {
                    if (faceToZone[allFacei] == -1)
                    {
                        faceToZone[allFacei] = allZoneI;
                    }
                    else if (faceToZone[allFacei] != allZoneI)
                    {
                        labelList& fZones = addFaceToZones[allFacei];

                        if (findIndex(fZones, allZoneI) == -1)
                        {
                            fZones.append(allZoneI);
                        }
                    }
                }
            }
        }
    }


    // Extract back into zones

    // 1. Count
    labelList nFaces(zoneNames.size(), 0);
    forAll(faceToZone, allFacei)
    {
        label zi = faceToZone[allFacei];
        if (zi != -1)
        {
            nFaces[zi]++;
        }
    }
    forAll(addFaceToZones, allFacei)
    {
        const labelList& fZones = addFaceToZones[allFacei];
        forAll(fZones, i)
        {
            nFaces[fZones[i]]++;
        }
    }

    // 2. Fill
    fzFaces.setSize(zoneNames.size());
    fzFlips.setSize(zoneNames.size());

    forAll(fzFaces, zi)
    {
        fzFaces[zi].setCapacity(nFaces[zi]);

        if (fzOrientations[zi])
        {
            fzFlips[zi].setCapacity(nFaces[zi]);
        }
    }

    forAll(faceToZone, allFacei)
    {
        label zi = faceToZone[allFacei];
        bool flip = faceToFlip[allFacei];
        if (zi != -1)
        {
            fzFaces[zi].append(allFacei);

            if (fzOrientations[zi])
            {
                fzFlips[zi].append(flip);
            }
        }
    }
    forAll(addFaceToZones, allFacei)
    {
        const labelList& fZones = addFaceToZones[allFacei];
        const boolList& flipZones = addFaceToFlips[allFacei];

        forAll(fZones, i)
        {
            label zi = fZones[i];
            fzFaces[zi].append(allFacei);
            if (fzOrientations[zi])
            {
                fzFlips[zi].append(flipZones[i]);
            }
        }
    }

    forAll(fzFaces, zi)
    {
        fzFaces[zi].shrink();
        fzFlips[zi].shrink();

        labelList order;
        sortedOrder(fzFaces[zi], order);
        fzFaces[zi] = UIndirectList<label>(fzFaces[zi], order)();
        fzFlips[zi] = UIndirectList<bool>(fzFlips[zi], order)();
    }
}


void Foam::polyMeshAdder::mergeCellZones
(
    const label nAllCells,

    const cellZoneList& cz0,
    const cellZoneList& cz1,
    const labelList& from1ToAllCells,

    DynamicList<word>& zoneNames,
    labelList& from1ToAll,
    List<DynamicList<label>>& czCells
)
{
    zoneNames.setCapacity(cz0.size() + cz1.size());
    zoneNames.append(cz0.toc());

    from1ToAll.setSize(cz1.size());
    forAll(cz1, zi)
    {
        from1ToAll[zi] = zoneIndex(cz1[zi].name(), zoneNames);
    }
    zoneNames.shrink();


    // Zone(s) per cell. Two levels: if only one zone
    // stored in cellToZone. Any extra stored in additionalCellToZones.
    // This is so we only allocate labelLists per cell if absolutely
    // necessary.
    labelList cellToZone(nAllCells, -1);
    labelListList addCellToZones(nAllCells);

    // mesh0 zones kept
    forAll(cz0, zi)
    {
        const cellZone& cz = cz0[zi];
        forAll(cz, i)
        {
            label cell0 = cz[i];

            if (cellToZone[cell0] == -1)
            {
                cellToZone[cell0] = zi;
            }
            else if (cellToZone[cell0] != zi)
            {
                labelList& cZones = addCellToZones[cell0];
                if (findIndex(cZones, zi) == -1)
                {
                    cZones.append(zi);
                }
            }
        }
    }

    // mesh1 zones renumbered
    forAll(cz1, zi)
    {
        const cellZone& cz = cz1[zi];
        const label allZoneI = from1ToAll[zi];
        forAll(cz, i)
        {
            label cell1 = cz[i];
            label allCelli = from1ToAllCells[cell1];

            if (cellToZone[allCelli] == -1)
            {
                cellToZone[allCelli] = allZoneI;
            }
            else if (cellToZone[allCelli] != allZoneI)
            {
                labelList& cZones = addCellToZones[allCelli];
                if (findIndex(cZones, allZoneI) == -1)
                {
                    cZones.append(allZoneI);
                }
            }
        }
    }

    // Extract back into zones

    // 1. Count
    labelList nCells(zoneNames.size(), 0);
    forAll(cellToZone, allCelli)
    {
        label zi = cellToZone[allCelli];
        if (zi != -1)
        {
            nCells[zi]++;
        }
    }
    forAll(addCellToZones, allCelli)
    {
        const labelList& cZones = addCellToZones[allCelli];
        forAll(cZones, i)
        {
            nCells[cZones[i]]++;
        }
    }

    // 2. Fill
    czCells.setSize(zoneNames.size());
    forAll(czCells, zi)
    {
        czCells[zi].setCapacity(nCells[zi]);
    }
    forAll(cellToZone, allCelli)
    {
        label zi = cellToZone[allCelli];
        if (zi != -1)
        {
            czCells[zi].append(allCelli);
        }
    }
    forAll(addCellToZones, allCelli)
    {
        const labelList& cZones = addCellToZones[allCelli];
        forAll(cZones, i)
        {
            czCells[cZones[i]].append(allCelli);
        }
    }
    forAll(czCells, i)
    {
        czCells[i].shrink();
        stableSort(czCells[i]);
    }
}


void Foam::polyMeshAdder::mergeZones
(
    const label nAllPoints,
    const labelList& allOwner,
    const label nAllCells,

    const polyMesh& mesh0,
    const polyMesh& mesh1,
    const labelList& from0ToAllPoints,
    const labelList& from0ToAllFaces,

    const labelList& from1ToAllPoints,
    const labelList& from1ToAllFaces,
    const labelList& from1ToAllCells,

    DynamicList<word>& pointZoneNames,
    List<DynamicList<label>>& pzPoints,

    DynamicList<word>& faceZoneNames,
    List<DynamicList<label>>& fzFaces,
    boolList& fzOrientations,
    List<DynamicList<bool>>& fzFlips,

    DynamicList<word>& cellZoneNames,
    List<DynamicList<label>>& czCells
)
{
    labelList from1ToAllPZones;
    mergePointZones
    (
        nAllPoints,
        mesh0.pointZones(),
        mesh1.pointZones(),
        from0ToAllPoints,
        from1ToAllPoints,

        pointZoneNames,
        from1ToAllPZones,
        pzPoints
    );

    labelList from1ToAllFZones;
    mergeFaceZones
    (
        allOwner,
        mesh0,
        mesh1,
        from0ToAllFaces,
        from1ToAllFaces,
        from1ToAllCells,

        faceZoneNames,
        from1ToAllFZones,
        fzFaces,
        fzOrientations,
        fzFlips
    );

    labelList from1ToAllCZones;
    mergeCellZones
    (
        nAllCells,
        mesh0.cellZones(),
        mesh1.cellZones(),
        from1ToAllCells,

        cellZoneNames,
        from1ToAllCZones,
        czCells
    );
}


void Foam::polyMeshAdder::addZones
(
    const DynamicList<word>& pointZoneNames,
    const List<DynamicList<label>>& pzPoints,

    const DynamicList<word>& faceZoneNames,
    const List<DynamicList<label>>& fzFaces,
    const boolList& fzOrientations,
    const List<DynamicList<bool>>& fzFlips,

    const DynamicList<word>& cellZoneNames,
    const List<DynamicList<label>>& czCells,

    polyMesh& mesh
)
{
    List<pointZone*> pZones(pzPoints.size());
    forAll(pZones, i)
    {
        pZones[i] = new pointZone
        (
            pointZoneNames[i],
            pzPoints[i],
            mesh.pointZones()
        );
    }

    List<faceZone*> fZones(fzFaces.size());
    forAll(fZones, i)
    {
        if (fzOrientations[i])
        {
            fZones[i] = new faceZone
            (
                faceZoneNames[i],
                fzFaces[i],
                fzFlips[i],
                mesh.faceZones()
            );
        }
        else
        {
            fZones[i] = new faceZone
            (
                faceZoneNames[i],
                fzFaces[i],
                mesh.faceZones()
            );
        }
    }

    List<cellZone*> cZones(czCells.size());
    forAll(cZones, i)
    {
        cZones[i] = new cellZone
        (
            cellZoneNames[i],
            czCells[i],
            mesh.cellZones()
        );
    }

    mesh.addZones
    (
        pZones,
        fZones,
        cZones
    );
}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

Foam::autoPtr<Foam::mapAddedPolyMesh> Foam::polyMeshAdder::add
(
    polyMesh& mesh0,
    const polyMesh& mesh1,
    const faceCoupleInfo& coupleInfo,
    const bool validBoundary
)
{
    const polyBoundaryMesh& patches0 = mesh0.boundaryMesh();
    const polyBoundaryMesh& patches1 = mesh1.boundaryMesh();

    DynamicList<word> allPatchNames(patches0.size() + patches1.size());
    DynamicList<word> allPatchTypes(allPatchNames.size());

    // Patch maps
    labelList from1ToAllPatches(patches1.size());
    labelList fromAllTo1Patches(allPatchNames.size(), -1);

    mergePatchNames
    (
        patches0,
        patches1,
        allPatchNames,
        allPatchTypes,
        from1ToAllPatches,
        fromAllTo1Patches
    );


    // New points
    pointField allPoints;

    // Map from mesh0/1 points to allPoints.
    labelList from0ToAllPoints(mesh0.nPoints(), -1);
    labelList from1ToAllPoints(mesh1.nPoints(), -1);

    // New faces
    faceList allFaces;
    labelList allOwner;
    labelList allNeighbour;
    label nInternalFaces;
    // Sizes per patch
    labelList nFaces(allPatchNames.size(), 0);
    label nCells;

    // Maps
    labelList from0ToAllFaces(mesh0.nFaces(), -1);
    labelList from1ToAllFaces(mesh1.nFaces(), -1);
    // Map
    labelList from1ToAllCells(mesh1.nCells(), -1);

    mergePrimitives
    (
        mesh0,
        mesh1,
        coupleInfo,

        allPatchNames.size(),
        fromAllTo1Patches,
        from1ToAllPatches,

        allPoints,
        from0ToAllPoints,
        from1ToAllPoints,

        allFaces,
        allOwner,
        allNeighbour,
        nInternalFaces,
        nFaces,
        nCells,

        from0ToAllFaces,
        from1ToAllFaces,
        from1ToAllCells
    );


    // Zones
    // ~~~~~

    DynamicList<word> pointZoneNames;
    List<DynamicList<label>> pzPoints;

    DynamicList<word> faceZoneNames;
    List<DynamicList<label>> fzFaces;
    boolList fzOrientations;
    List<DynamicList<bool>> fzFlips;

    DynamicList<word> cellZoneNames;
    List<DynamicList<label>> czCells;

    mergeZones
    (
        allPoints.size(),
        allOwner,
        nCells,

        mesh0,
        mesh1,

        from0ToAllPoints,
        from0ToAllFaces,

        from1ToAllPoints,
        from1ToAllFaces,
        from1ToAllCells,

        pointZoneNames,
        pzPoints,

        faceZoneNames,
        fzFaces,
        fzOrientations,
        fzFlips,

        cellZoneNames,
        czCells
    );


    // Patches
    // ~~~~~~~


    // Store mesh0 patch info before modifying patches0.
    labelList mesh0PatchSizes(getPatchSizes(patches0));
    labelList mesh0PatchStarts(getPatchStarts(patches0));

    // Map from 0 to all patches (since gets compacted)
    labelList from0ToAllPatches(patches0.size(), -1);

    // Inplace extend mesh0 patches (note that patches0.size() now also
    // has changed)
    labelList patchSizes(allPatchNames.size());
    labelList patchStarts(allPatchNames.size());

    label startFacei = nInternalFaces;

    // Copy patches0 with new sizes. First patches always come from
    // mesh0 and will always be present.
    label allPatchi = 0;

    forAll(from0ToAllPatches, patch0)
    {
        // Originates from mesh0. Clone with new size & filter out empty
        // patch.

        if (nFaces[patch0] == 0 && isA<processorPolyPatch>(patches0[patch0]))
        {
            // Pout<< "Removing zero sized mesh0 patch "
            //     << allPatchNames[patch0]
            //     << endl;
            from0ToAllPatches[patch0] = -1;

            // Check if patch was also in mesh1 and update its addressing if so.
            if (fromAllTo1Patches[patch0] != -1)
            {
                from1ToAllPatches[fromAllTo1Patches[patch0]] = -1;
            }
        }
        else
        {
            patchSizes[allPatchi] = nFaces[patch0];
            patchStarts[allPatchi] = startFacei;

            // Record new index in allPatches
            from0ToAllPatches[patch0] = allPatchi;

            // Check if patch was also in mesh1 and update its addressing if so.
            if (fromAllTo1Patches[patch0] != -1)
            {
                from1ToAllPatches[fromAllTo1Patches[patch0]] = allPatchi;
            }

            startFacei += nFaces[patch0];

            allPatchi++;
        }
    }

    // Trim the existing patches
    {
        const label sz0 = from0ToAllPatches.size();
        labelList newToOld(sz0, sz0-1);
        label nNew = 0;
        forAll(from0ToAllPatches, patchi)
        {
            if (from0ToAllPatches[patchi] != -1)
            {
                newToOld[nNew++] = patchi;
            }
        }
        newToOld.setSize(nNew);
        mesh0.reorderPatches(newToOld, false);
    }

    // Copy unique patches of mesh1.
    forAll(from1ToAllPatches, patch1)
    {
        label uncompactAllPatchi = from1ToAllPatches[patch1];

        if (uncompactAllPatchi >= from0ToAllPatches.size())
        {
            // Patch has not been merged with any mesh0 patch.

            if
            (
                nFaces[uncompactAllPatchi] == 0
             && isA<processorPolyPatch>(patches1[patch1])
            )
            {
                // Pout<< "Removing zero sized mesh1 patch "
                //    << allPatchNames[uncompactAllPatchi] << endl;
                from1ToAllPatches[patch1] = -1;
            }
            else
            {
                patchSizes[allPatchi] = nFaces[uncompactAllPatchi];
                patchStarts[allPatchi] = startFacei;

                // Clone. Note dummy size and start. Gets overwritten later in
                // resetPrimitives. This avoids getting temporarily illegal
                // SubList construction in polyPatch.
                mesh0.addPatch(allPatchi, patches1[patch1]);

                // Record new index in allPatches
                from1ToAllPatches[patch1] = allPatchi;

                startFacei += nFaces[uncompactAllPatchi];

                allPatchi++;
            }
        }
    }
    patchSizes.setSize(allPatchi);
    patchStarts.setSize(allPatchi);


    // Construct map information before changing mesh0 primitives
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    autoPtr<mapAddedPolyMesh> mapPtr
    (
        new mapAddedPolyMesh
        (
            mesh0.nPoints(),
            mesh0.nFaces(),
            mesh0.nCells(),

            mesh1.nPoints(),
            mesh1.nFaces(),
            mesh1.nCells(),

            from0ToAllPoints,
            from0ToAllFaces,
            identityMap(mesh0.nCells()),

            from1ToAllPoints,
            from1ToAllFaces,
            from1ToAllCells,

            from0ToAllPatches,
            from1ToAllPatches,

            mesh0PatchSizes,
            mesh0PatchStarts
        )
    );



    // Now we have extracted all information from all meshes.
    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    mesh0.resetMotion();    // delete any oldPoints.
    mesh0.resetPrimitives
    (
        move(allPoints),
        move(allFaces),
        move(allOwner),
        move(allNeighbour),
        patchSizes,     // size
        patchStarts,    // patchstarts
        validBoundary   // boundary valid?
    );

    // Add zones to new mesh.
    mesh0.pointZones().clear();
    mesh0.faceZones().clear();
    mesh0.cellZones().clear();
    addZones
    (
        pointZoneNames,
        pzPoints,

        faceZoneNames,
        fzFaces,
        fzOrientations,
        fzFlips,

        cellZoneNames,
        czCells,
        mesh0
    );

    return mapPtr;
}


// ************************************************************************* //
