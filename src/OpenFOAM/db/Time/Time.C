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

#include "Time.H"
#include "timeIOdictionary.H"
#include "PstreamReduceOps.H"
#include "argList.H"

// * * * * * * * * * * * * * Static Member Data  * * * * * * * * * * * * * * //

namespace Foam
{
    defineTypeNameAndDebug(Time, 0);
}

const Foam::NamedEnum<Foam::Time::stopAtControl, 4>
Foam::Time::stopAtControlNames
{
    "endTime",
    "noWriteNow",
    "writeNow",
    "nextWrite"
};

const Foam::NamedEnum<Foam::Time::writeControl, 5>
Foam::Time::writeControlNames
{
    "timeStep",
    "runTime",
    "adjustableRunTime",
    "clockTime",
    "cpuTime"
};

Foam::Time::format Foam::Time::format_(Foam::Time::format::general);

int Foam::Time::precision_(6);

int Foam::Time::curPrecision_(Foam::Time::precision_);

const int Foam::Time::maxPrecision_(3 - log10(small));

Foam::word Foam::Time::controlDictName("controlDict");


// * * * * * * * * * * * * Protected Member Functions  * * * * * * * * * * * //

void Foam::Time::adjustDeltaT()
{
    const scalar timeToNextAction = min
    (
        max
        (
            0,
            (writeTimeIndex_ + 1)*writeInterval_ - (value() - beginTime_)
        ),
        functionObjects_.timeToNextAction()
    );

    const scalar nSteps = timeToNextAction/deltaT_;

    if (nSteps < labelMax)
    {
        // Allow the time-step to increase by up to 1%
        // to accommodate the next write time before splitting
        const label nStepsToNextAction = label(nSteps + 0.99);

        // Adjust deltaT if nStepsToNextWrite > 0
        if (nStepsToNextAction > 0)
        {
            deltaT_ = timeToNextAction/nStepsToNextAction;
        }
    }
}


void Foam::Time::setControls()
{
    // default is to resume calculation from "latestTime"
    const word startFrom = controlDict_.lookupOrDefault<word>
    (
        "startFrom",
        "latestTime"
    );

    if (startFrom == "startTime")
    {
        startTime_ = controlDict_.lookup<scalar>("startTime", userUnits());
    }
    else
    {
        // Search directory for valid time directories
        const instantList timeDirs = findTimes(path(), constant());

        if (startFrom == "firstTime")
        {
            if (timeDirs.size())
            {
                if (timeDirs[0].name() == constant() && timeDirs.size() >= 2)
                {
                    startTime_ = userTimeToTime(timeDirs[1].value());
                }
                else
                {
                    startTime_ = userTimeToTime(timeDirs[0].value());
                }
            }
        }
        else if (startFrom == "latestTime")
        {
            if (timeDirs.size())
            {
                startTime_ = userTimeToTime(timeDirs.last().value());
            }
        }
        else
        {
            FatalIOErrorInFunction(controlDict_)
                << "expected startTime, firstTime or latestTime"
                << " found '" << startFrom << "'"
                << exit(FatalIOError);
        }
    }

    setTime(startTime_, 0);
    readDict();
    deltaTSave_ = deltaT_;
    deltaT0_ = deltaT_;

    // Check if time directory exists
    // If not increase time precision to see if it is formatted differently.
    if (!fileHandler().exists(timePath(), false, false))
    {
        int oldPrecision = curPrecision_;
        int requiredPrecision = -1;

        for
        (
            curPrecision_ = maxPrecision_;
            curPrecision_ > oldPrecision;
            curPrecision_--
        )
        {
            // Update the time formatting
            setTime(startTime_, 0);

            // Check the existence of the time directory with the new format
            if (fileHandler().exists(timePath(), false, false))
            {
                requiredPrecision = curPrecision_;
            }
        }

        if (requiredPrecision > 0)
        {
            // Update the time precision
            curPrecision_ = requiredPrecision;

            // Update the time formatting
            setTime(startTime_, 0);

            WarningInFunction
                << "Increasing the timePrecision from " << oldPrecision
                << " to " << curPrecision_
                << " to support the formatting of the current time directory "
                << name() << nl << endl;
        }
        else
        {
            // Could not find time directory so assume it is not present
            curPrecision_ = oldPrecision;

            // Revert the time formatting
            setTime(startTime_, 0);
        }
    }

    if (Pstream::parRun())
    {
        scalar sumStartTime = startTime_;
        reduce(sumStartTime, sumOp<scalar>());
        if
        (
            mag(Pstream::nProcs()*startTime_ - sumStartTime)
          > Pstream::nProcs()*deltaT_/10.0
        )
        {
            FatalIOErrorInFunction(controlDict_)
                << "Start time is not the same for all processors" << nl
                << "processor " << Pstream::myProcNo() << " has startTime "
                << startTime_ << exit(FatalIOError);
        }
    }

    timeIOdictionary timeDict
    (
        IOobject
        (
            "time",
            name(),
            "uniform",
            *this,
            IOobject::READ_IF_PRESENT,
            IOobject::NO_WRITE,
            false
        )
    );

    if (controlDict_.found("beginTime"))
    {
        beginTime_ = controlDict_.lookup<scalar>("beginTime", userUnits());
    }
    else if (timeDict.found("beginTime"))
    {
        beginTime_ = timeDict.lookup<scalar>("beginTime", userUnits());
    }
    else
    {
        beginTime_ = startTime_;
    }

    // Read and set the deltaT only if time-step adjustment is active
    // otherwise use the deltaT from the controlDict
    if (controlDict_.lookupOrDefault<Switch>("adjustTimeStep", false))
    {
        if (timeDict.found("deltaT"))
        {
            deltaT_ = timeDict.lookup<scalar>("deltaT", userUnits());
            deltaTSave_ = deltaT_;
            deltaT0_ = deltaT_;
        }
    }

    if (timeDict.found("deltaT0"))
    {
        deltaT0_ = timeDict.lookup<scalar>("deltaT0", userUnits());
    }

    if (timeDict.readIfPresent("index", startTimeIndex_))
    {
        timeIndex_ = startTimeIndex_;
    }

    // Set writeTimeIndex_ to correspond to beginTime_
    if
    (
        (
            writeControl_ == writeControl::runTime
         || writeControl_ == writeControl::adjustableRunTime
        )
    )
    {
        writeTimeIndex_ = label
        (
            ((value() - beginTime_) + 0.5*deltaT_)/writeInterval_
        );
    }


    // Check if values stored in time dictionary are consistent

    // 1. Based on time name
    bool checkValue = true;

    string storedTimeName;
    if (timeDict.readIfPresent("name", storedTimeName))
    {
        if (storedTimeName == name())
        {
            // Same time. No need to check stored value
            checkValue = false;
        }
    }

    // 2. Based on time value
    //    (consistent up to the current time writing precision so it won't
    //     trigger if we just change the write precision)
    if (checkValue)
    {
        scalar storedTimeValue;
        if (timeDict.readIfPresent("value", storedTimeValue))
        {
            word storedTimeName(timeName(storedTimeValue));

            if (storedTimeName != name())
            {
                IOWarningInFunction(timeDict)
                    << "Time read from time dictionary " << storedTimeName
                    << " differs from actual time " << name() << '.' << nl
                    << "    This may cause unexpected database behaviour."
                    << " If you are not interested" << nl
                    << "    in preserving time state delete"
                    << " the time dictionary."
                    << endl;
            }
        }
    }
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::Time::Time
(
    const word& controlDictName,
    const argList& args,
    const bool enableFunctionObjects
)
:
    TimePaths
    (
        args.parRunControl().parRun(),
        args.rootPath(),
        args.globalCaseName(),
        args.caseName()
    ),

    objectRegistry(*this),

    runTimeModifiable_(false),

    controlDict_
    (
        IOobject
        (
            controlDictName,
            system(),
            *this,
            IOobject::MUST_READ_IF_MODIFIED,
            IOobject::NO_WRITE
        ),
        *this
    ),

    startTimeIndex_(0),
    startTime_(0),
    endTime_(0),
    beginTime_(startTime_),

    userTime_(userTimes::userTime::New(controlDict_)),

    stopAt_(stopAtControl::endTime),
    writeControl_(writeControl::timeStep),
    writeInterval_(great),
    purgeWrite_(0),
    writeOnce_(false),

    subCycling_(false),

    sigWriteNow_(writeInfoHeader, *this),
    sigStopAtWriteNow_(writeInfoHeader, *this),

    writeFormat_(IOstream::ASCII),
    writeVersion_(IOstream::currentVersion),
    writeCompression_(IOstream::UNCOMPRESSED),
    cacheTemporaryObjects_(true),

    functionObjects_
    (
        *this,
        enableFunctionObjects
      ? argList::validOptions.found("functionObjects")
        ? args.optionFound("functionObjects")
        : !args.optionFound("noFunctionObjects")
      : false
    )
{
    libs.open(controlDict_, "libs");

    // Explicitly set read flags on objectRegistry so anything constructed
    // from it reads as well (e.g. fvSolution).
    readOpt() = IOobject::MUST_READ_IF_MODIFIED;

    if (args.options().found("case"))
    {
        const wordList switchSets
        (
            {
                "InfoSwitches",
                "OptimisationSwitches",
                "DebugSwitches",
                "DimensionedConstants",
                "DimensionSets",
                "UnitConversions"
            }
        );

        forAll(switchSets, i)
        {
            if (controlDict_.found(switchSets[i]))
            {
                IOWarningInFunction(controlDict_)
                    << switchSets[i]
                    << " in system/controlDict are only processed if "
                    << args.executable() << " is run in the "
                    << args.path() << " directory" << endl;
            }
        }
    }

    setControls();

    // Add a watch on the controlDict and functions files
    // after runTimeModifiable_ is set
    controlDict_.addWatch();
    functionObjects_.addWatch();
}


Foam::Time::Time
(
    const word& controlDictName,
    const fileName& rootPath,
    const fileName& caseName,
    const bool enableFunctionObjects
)
:
    TimePaths(rootPath, caseName),

    objectRegistry(*this),

    runTimeModifiable_(false),

    controlDict_
    (
        IOobject
        (
            controlDictName,
            system(),
            *this,
            IOobject::MUST_READ_IF_MODIFIED,
            IOobject::NO_WRITE
        ),
        *this
    ),

    startTimeIndex_(0),
    startTime_(0),
    endTime_(0),
    beginTime_(startTime_),

    userTime_(userTimes::userTime::New(controlDict_)),

    stopAt_(stopAtControl::endTime),
    writeControl_(writeControl::timeStep),
    writeInterval_(great),
    purgeWrite_(0),
    writeOnce_(false),

    subCycling_(false),

    sigWriteNow_(writeInfoHeader, *this),
    sigStopAtWriteNow_(writeInfoHeader, *this),

    writeFormat_(IOstream::ASCII),
    writeVersion_(IOstream::currentVersion),
    writeCompression_(IOstream::UNCOMPRESSED),
    cacheTemporaryObjects_(true),

    functionObjects_(*this, enableFunctionObjects)
{
    libs.open(controlDict_, "libs");

    // Explicitly set read flags on objectRegistry so anything constructed
    // from it reads as well (e.g. fvSolution).
    readOpt() = IOobject::MUST_READ_IF_MODIFIED;

    setControls();

    // Add a watch on the controlDict and functions files
    // after runTimeModifiable_ is set
    controlDict_.addWatch();
    functionObjects_.addWatch();
}


Foam::Time::Time
(
    const dictionary& dict,
    const fileName& rootPath,
    const fileName& caseName,
    const bool enableFunctionObjects
)
:
    TimePaths(rootPath, caseName),

    objectRegistry(*this),

    runTimeModifiable_(false),

    controlDict_
    (
        IOobject
        (
            controlDictName,
            system(),
            *this,
            IOobject::MUST_READ_IF_MODIFIED,
            IOobject::NO_WRITE
        ),
        dict,
        *this
    ),

    startTimeIndex_(0),
    startTime_(0),
    endTime_(0),
    beginTime_(startTime_),

    userTime_(userTimes::userTime::New(controlDict_)),

    stopAt_(stopAtControl::endTime),
    writeControl_(writeControl::timeStep),
    writeInterval_(great),
    purgeWrite_(0),
    writeOnce_(false),

    subCycling_(false),

    sigWriteNow_(writeInfoHeader, *this),
    sigStopAtWriteNow_(writeInfoHeader, *this),

    writeFormat_(IOstream::ASCII),
    writeVersion_(IOstream::currentVersion),
    writeCompression_(IOstream::UNCOMPRESSED),
    cacheTemporaryObjects_(true),

    functionObjects_(*this, enableFunctionObjects)
{
    libs.open(controlDict_, "libs");

    // Explicitly set read flags on objectRegistry so anything constructed
    // from it reads as well (e.g. fvSolution).
    readOpt() = IOobject::MUST_READ_IF_MODIFIED;

    setControls();

    // Add a watch on the controlDict and functions files
    // after runTimeModifiable_ is set
    controlDict_.addWatch();
    functionObjects_.addWatch();
}


Foam::Time::Time
(
    const fileName& rootPath,
    const fileName& caseName,
    const bool enableFunctionObjects
)
:
    TimePaths(rootPath, caseName),

    objectRegistry(*this),

    runTimeModifiable_(false),

    controlDict_
    (
        IOobject
        (
            controlDictName,
            system(),
            *this,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        *this
    ),

    startTimeIndex_(0),
    startTime_(0),
    endTime_(0),
    beginTime_(startTime_),

    userTime_(userTimes::userTime::New(controlDict_)),

    stopAt_(stopAtControl::endTime),
    writeControl_(writeControl::timeStep),
    writeInterval_(great),
    purgeWrite_(0),
    writeOnce_(false),

    subCycling_(false),

    writeFormat_(IOstream::ASCII),
    writeVersion_(IOstream::currentVersion),
    writeCompression_(IOstream::UNCOMPRESSED),
    cacheTemporaryObjects_(true),

    functionObjects_(*this, enableFunctionObjects)
{
    libs.open(controlDict_, "libs");
}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

Foam::Time::~Time()
{
    // Destroy function objects first
    functionObjects_.clear();
}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

Foam::word Foam::Time::timeName(const scalar t, const int precision)
{
    std::ostringstream buf;
    buf.setf(ios_base::fmtflags(format_), ios_base::floatfield);
    buf.precision(precision);
    buf << t;
    return buf.str();
}


Foam::instantList Foam::Time::times() const
{
    return findTimes(path(), constant());
}


Foam::word Foam::Time::findInstance
(
    const fileName& dir,
    const word& name,
    const IOobject::readOption rOpt,
    const word& stopInstance
) const
{
    IOobject startIO
    (
        name,           // name might be empty!
        dimensionedScalar::name(),
        dir,
        *this,
        rOpt
    );

    IOobject io
    (
        fileHandler().findInstance
        (
            startIO,
            userTimeValue(),
            stopInstance
        )
    );

    return io.instance();
}


Foam::word Foam::Time::findInstancePath
(
    const fileName& directory,
    const instant& t
) const
{
    // Simplified version: use findTimes (readDir + sort). The expensive
    // bit is the readDir, not the sorting. Tbd: avoid calling findInstancePath
    // from filePath.

    const instantList timeDirs = findTimes(path(), constant());
    // Note:
    // - times will include constant (with value 0) as first element.
    //   For backwards compatibility make sure to find 0 in preference
    //   to constant.
    // - list is sorted so could use binary search

    forAllReverse(timeDirs, i)
    {
        if (t.equal(timeDirs[i].value()))
        {
            return timeDirs[i].name();
        }
    }

    return word::null;
}


Foam::word Foam::Time::findInstancePath(const instant& t) const
{
    return findInstancePath(path(), t);
}


Foam::instant Foam::Time::findClosestTime(const scalar t) const
{
    const instantList timeDirs = findTimes(path(), constant());

    // There is only one time (likely "constant") so return it
    if (timeDirs.size() == 1)
    {
        return timeDirs[0];
    }

    if (t < timeDirs[1].value())
    {
        return timeDirs[1];
    }
    else if (t > timeDirs.last().value())
    {
        return timeDirs.last();
    }

    label nearestIndex = -1;
    scalar deltaT = great;

    for (label timei=1; timei < timeDirs.size(); ++timei)
    {
        scalar diff = mag(timeDirs[timei].value() - t);
        if (diff < deltaT)
        {
            deltaT = diff;
            nearestIndex = timei;
        }
    }

    return timeDirs[nearestIndex];
}


Foam::label Foam::Time::findClosestTimeIndex
(
    const instantList& timeDirs,
    const scalar t,
    const word& constantName
)
{
    label nearestIndex = -1;
    scalar deltaT = great;

    forAll(timeDirs, timei)
    {
        if (timeDirs[timei].name() == constantName) continue;

        scalar diff = mag(timeDirs[timei].value() - t);
        if (diff < deltaT)
        {
            deltaT = diff;
            nearestIndex = timei;
        }
    }

    return nearestIndex;
}


Foam::label Foam::Time::startTimeIndex() const
{
    return startTimeIndex_;
}


Foam::dimensionedScalar Foam::Time::beginTime() const
{
    return dimensionedScalar
    (
        timeName(timeToUserTime(beginTime_)),
        dimTime,
        beginTime_
    );
}


Foam::dimensionedScalar Foam::Time::startTime() const
{
    return dimensionedScalar
    (
        timeName(timeToUserTime(startTime_)),
        dimTime,
        startTime_
    );
}


Foam::dimensionedScalar Foam::Time::endTime() const
{
    return dimensionedScalar
    (
        timeName(timeToUserTime(endTime_)),
        dimTime,
        endTime_
    );
}


const Foam::userTimes::userTime& Foam::Time::userTime() const
{
    return *userTime_;
}


Foam::scalar Foam::Time::userTimeValue() const
{
    return userTime_->timeToUserTime(value());
}


Foam::scalar Foam::Time::userDeltaTValue() const
{
    return
        userTime_->timeToUserTime(value())
      - userTime_->timeToUserTime(value() - deltaT_);
}


Foam::scalar Foam::Time::userTimeToTime(const scalar tau) const
{
    return userTime_->userTimeToTime(tau);
}


Foam::scalar Foam::Time::timeToUserTime(const scalar t) const
{
    return userTime_->timeToUserTime(t);
}


Foam::word Foam::Time::userTimeName() const
{
    if (name() == constant())
    {
        return constant();
    }
    else
    {
        return timeName(userTimeValue()) + userTime_->unitName();
    }
}


const Foam::unitConversion& Foam::Time::userUnits() const
{
    return userTime_->units();
}


const Foam::unitConversion& Foam::Time::writeIntervalUnits() const
{
    static const unitConversion unitSeconds(dimTime);

    switch (writeControl_)
    {
        case writeControl::timeStep:
            return unitless;
        case writeControl::runTime:
        case writeControl::adjustableRunTime:
            return userUnits();
        case writeControl::cpuTime:
        case writeControl::clockTime:
            return unitSeconds;
    }

    return unitNone;
}


bool Foam::Time::running() const
{
    return value() < (endTime_ - 0.5*deltaT_);
}


bool Foam::Time::run() const
{
    bool running = this->running();

    if (!subCycling_)
    {
        if (!running && timeIndex_ != startTimeIndex_)
        {
            functionObjects_.execute();
            functionObjects_.end();

            if (cacheTemporaryObjects_)
            {
                cacheTemporaryObjects_ = checkCacheTemporaryObjects();
            }
        }
    }

    if (running)
    {
        if (!subCycling_)
        {
            const_cast<Time&>(*this).readModifiedObjects();

            if (timeIndex_ == startTimeIndex_)
            {
                functionObjects_.start();
            }
            else
            {
                functionObjects_.execute();

                if (cacheTemporaryObjects_)
                {
                    cacheTemporaryObjects_ = checkCacheTemporaryObjects();
                }
            }
        }

        // Re-evaluate if running in case a function object has changed things
        running = this->running();
    }

    return running;
}


bool Foam::Time::loop()
{
    bool running = run();

    if (running)
    {
        operator++();
    }

    return running;
}


bool Foam::Time::end() const
{
    return value() > (endTime_ + 0.5*deltaT_);
}


bool Foam::Time::stopAt(const stopAtControl sa) const
{
    const bool changed = (stopAt_ != sa);
    stopAt_ = sa;

    // adjust endTime
    if (sa == stopAtControl::endTime)
    {
        controlDict_.lookup("endTime") >> endTime_;
    }
    else
    {
        endTime_ = great;
    }
    return changed;
}


void Foam::Time::setTime(const Time& t)
{
    value() = t.value();
    dimensionedScalar::name() = t.dimensionedScalar::name();
    timeIndex_ = t.timeIndex_;
    fileHandler().setTime(*this);
}


void Foam::Time::setTime(const instant& inst, const label newIndex)
{
    value() = userTimeToTime(inst.value());
    dimensionedScalar::name() = inst.name();
    timeIndex_ = newIndex;

    timeIOdictionary timeDict
    (
        IOobject
        (
            "time",
            name(),
            "uniform",
            *this,
            IOobject::READ_IF_PRESENT,
            IOobject::NO_WRITE,
            false
        )
    );

    if (timeDict.found("deltaT"))
    {
        deltaT_ = timeDict.lookup<scalar>("deltaT", userUnits());
    }

    if (timeDict.found("deltaT0"))
    {
        deltaT0_ = timeDict.lookup<scalar>("deltaT0", userUnits());
    }

    timeDict.readIfPresent("index", timeIndex_);

    fileHandler().setTime(*this);
}


void Foam::Time::setTime(const dimensionedScalar& newTime, const label newIndex)
{
    setTime(newTime.value(), newIndex);
}


void Foam::Time::setTime(const scalar newTime, const label newIndex)
{
    value() = newTime;
    dimensionedScalar::name() = timeName(timeToUserTime(newTime));
    timeIndex_ = newIndex;
    fileHandler().setTime(*this);
}


void Foam::Time::setEndTime(const dimensionedScalar& endTime)
{
    setEndTime(endTime.value());
}


void Foam::Time::setEndTime(const scalar endTime)
{
    endTime_ = endTime;
}


void Foam::Time::setDeltaT(const dimensionedScalar& deltaT)
{
    setDeltaT(deltaT.value());
}


void Foam::Time::setDeltaT(const scalar deltaT)
{
    setDeltaTNoAdjust(deltaT);

    if (writeControl_ == writeControl::adjustableRunTime)
    {
        adjustDeltaT();
    }
}


void Foam::Time::setDeltaTNoAdjust(const scalar deltaT)
{
    deltaT_ = deltaT;
    deltaTchanged_ = true;
}


void Foam::Time::setWriteInterval(const scalar writeInterval)
{
    if (writeInterval_ == great || !equal(writeInterval, writeInterval_))
    {
        writeInterval_ = writeInterval;

        if
        (
            writeControl_ == writeControl::runTime
         || writeControl_ == writeControl::adjustableRunTime
        )
        {
            // Recalculate writeTimeIndex_ for consistency with the new
            // writeInterval
            writeTimeIndex_ = label
            (
                ((value() - beginTime_) + 0.5*deltaT_)/writeInterval_
            );
        }
        else if (writeControl_ == writeControl::timeStep)
        {
            // Set to the nearest integer
            writeInterval_ = label(writeInterval + 0.5);
        }
    }
}


Foam::TimeState Foam::Time::subCycle(const label nSubCycles)
{
    subCycling_ = true;
    prevTimeState_.set(new TimeState(*this));

    setTime(*this - deltaT(), (timeIndex() - 1)*nSubCycles);
    deltaT_ /= nSubCycles;
    deltaT0_ /= nSubCycles;
    deltaTSave_ = deltaT0_;

    return prevTimeState();
}


void Foam::Time::endSubCycle()
{
    if (subCycling_)
    {
        subCycling_ = false;
        TimeState::operator=(prevTimeState());
        prevTimeState_.clear();
    }
}


// * * * * * * * * * * * * * * * Member Operators  * * * * * * * * * * * * * //

Foam::Time& Foam::Time::operator+=(const dimensionedScalar& deltaT)
{
    return operator+=(deltaT.value());
}


Foam::Time& Foam::Time::operator+=(const scalar deltaT)
{
    setDeltaT(deltaT);
    return operator++();
}


Foam::Time& Foam::Time::operator++()
{
    deltaT0_ = deltaTSave_;
    deltaTSave_ = deltaT_;

    // Save old time value and name
    const scalar oldTimeValue = timeToUserTime(value());
    const word oldTimeName = dimensionedScalar::name();

    // Increment time
    setTime(value() + deltaT_, timeIndex_ + 1);

    if (!subCycling_)
    {
        // If the time is very close to zero reset to zero
        if (mag(value()) < 10*small*deltaT_)
        {
            setTime(0, timeIndex_);
        }

        if (sigStopAtWriteNow_.active() || sigWriteNow_.active())
        {
            // A signal might have been sent on one processor only
            // Reduce so all decide the same.

            label flag = 0;
            if
            (
                sigStopAtWriteNow_.active()
             && stopAt_ == stopAtControl::writeNow
            )
            {
                flag += 1;
            }
            if (sigWriteNow_.active() && writeOnce_)
            {
                flag += 2;
            }
            reduce(flag, maxOp<label>());

            if (flag & 1)
            {
                stopAt_ = stopAtControl::writeNow;
            }
            if (flag & 2)
            {
                writeOnce_ = true;
            }
        }

        writeTime_ = false;

        switch (writeControl_)
        {
            case writeControl::timeStep:
                writeTime_ = !(timeIndex_ % label(writeInterval_));
            break;

            case writeControl::runTime:
            case writeControl::adjustableRunTime:
            {
                label writeIndex = label
                (
                    ((value() - beginTime_) + 0.5*deltaT_)
                  / writeInterval_
                );

                if (writeIndex > writeTimeIndex_)
                {
                    writeTime_ = true;
                    writeTimeIndex_ = writeIndex;
                }
            }
            break;

            case writeControl::cpuTime:
            {
                label writeIndex = label
                (
                    returnReduce(elapsedCpuTime(), maxOp<double>())
                  / writeInterval_
                );
                if (writeIndex > writeTimeIndex_)
                {
                    writeTime_ = true;
                    writeTimeIndex_ = writeIndex;
                }
            }
            break;

            case writeControl::clockTime:
            {
                label writeIndex = label
                (
                    returnReduce(label(elapsedClockTime()), maxOp<label>())
                  / writeInterval_
                );
                if (writeIndex > writeTimeIndex_)
                {
                    writeTime_ = true;
                    writeTimeIndex_ = writeIndex;
                }
            }
            break;
        }


        // Check if endTime needs adjustment to stop at the next run()/end()
        if (!end())
        {
            if (stopAt_ == stopAtControl::noWriteNow)
            {
                endTime_ = value();
            }
            else if (stopAt_ == stopAtControl::writeNow)
            {
                endTime_ = value();
                writeTime_ = true;
            }
            else if (stopAt_ == stopAtControl::nextWrite && writeTime_ == true)
            {
                endTime_ = value();
            }
        }

        // Override writeTime if one-shot writing
        if (writeOnce_)
        {
            writeTime_ = true;
            writeOnce_ = false;
        }

        // Adjust the precision of the time name if necessary
        {
            // User-time equivalent of deltaT
            const scalar userDeltaT = userDeltaTValue();

            // Tolerance used when testing time equivalence
            const scalar timeTol =
                max(min(pow(10.0, -precision_), 0.1*userDeltaT), small);

            // Time value obtained by reading timeName
            scalar timeNameValue = -vGreat;

            // Check that new time representation differs from old one
            // reinterpretation of the word
            if
            (
                readScalar(dimensionedScalar::name().c_str(), timeNameValue)
             && (mag(timeNameValue - oldTimeValue - userDeltaT) > timeTol)
            )
            {
                int oldPrecision = curPrecision_;
                while
                (
                    curPrecision_ < maxPrecision_
                 && readScalar(dimensionedScalar::name().c_str(), timeNameValue)
                 && (mag(timeNameValue - oldTimeValue - userDeltaT) > timeTol)
                )
                {
                    curPrecision_++;
                    setTime(value(), timeIndex());
                }

                if (curPrecision_ != oldPrecision)
                {
                    WarningInFunction
                        << "Increased the timePrecision from " << oldPrecision
                        << " to " << curPrecision_
                        << " to distinguish between timeNames at time "
                        << dimensionedScalar::name()
                        << endl;

                    if (curPrecision_ == maxPrecision_)
                    {
                        // Reached maxPrecision limit
                        FatalErrorInFunction
                            << "Current time name " << dimensionedScalar::name()
                            << nl
                            << "    The maximum time precision has been reached"
                               " which might result in overwriting previous"
                               " results."
                            << exit(FatalError);
                    }

                    // Check if round-off error caused time-reversal
                    scalar oldTimeNameValue = -vGreat;
                    if
                    (
                        readScalar(oldTimeName.c_str(), oldTimeNameValue)
                     && (
                            sign(timeNameValue - oldTimeNameValue)
                         != sign(deltaT_)
                        )
                    )
                    {
                        WarningInFunction
                            << "Current time name " << dimensionedScalar::name()
                            << " is set to an instance prior to the "
                               "previous one "
                            << oldTimeName << nl
                            << "    This might result in temporal "
                               "discontinuities."
                            << endl;
                    }
                }
            }
        }
    }

    return *this;
}


Foam::Time& Foam::Time::operator++(int)
{
    return operator++();
}


// ************************************************************************* //
