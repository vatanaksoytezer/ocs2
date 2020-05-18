/******************************************************************************
Copyright (c) 2020, Farbod Farshidian. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the copyright holder nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
******************************************************************************/

// #include <algorithm>
// #include <array>
// #include <memory>
// #include <numeric>
// #include <type_traits>

#include <iomanip>
#include <iostream>

#include <ocs2_core/OCS2NumericTraits.h>
#include <ocs2_core/misc/Numerics.h>

#include <ocs2_oc/rollout/RolloutBase.h>

namespace ocs2 {

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
vector_t RolloutBase::run(scalar_t initTime, const vector_t& initState, scalar_t finalTime, ControllerBase* controller,
                          const scalar_array_t& eventTimes, scalar_array_t& timeTrajectory, size_array_t& postEventIndicesStock,
                          vector_array_t& stateTrajectory, vector_array_t& inputTrajectory,
                          ModelDataBase::array_t* modelDataTrajectoryPtr /*= nullptr*/) {
  if (initTime > finalTime) {
    throw std::runtime_error("Initial time should be less-equal to final time.");
  }

  // switching times
  auto firstIndex = std::upper_bound(eventTimes.begin(), eventTimes.end(), initTime);
  auto lastIndex = std::upper_bound(eventTimes.begin(), eventTimes.end(), finalTime);
  scalar_array_t switchingTimes;
  switchingTimes.push_back(initTime);
  switchingTimes.insert(switchingTimes.end(), firstIndex, lastIndex);
  switchingTimes.push_back(finalTime);

  // constructing the rollout time intervals
  time_interval_array_t timeIntervalArray;
  const int numSubsystems = switchingTimes.size() - 1;
  for (int i = 0; i < numSubsystems; i++) {
    const auto& beginTime = switchingTimes[i];
    const auto& endTime = switchingTimes[i + 1];
    timeIntervalArray.emplace_back(beginTime, endTime);

    // adjusting the start time for correcting the subsystem recognition
    const scalar_t eps = OCS2NumericTraits<scalar_t>::weakEpsilon();
    if (endTime - beginTime > eps) {
      timeIntervalArray.back().first += eps;
    } else {
      timeIntervalArray.back().first = endTime;
    }
  }  // end of for loop

  return runImpl(std::move(timeIntervalArray), initState, controller, timeTrajectory, postEventIndicesStock, stateTrajectory,
                 inputTrajectory, modelDataTrajectoryPtr);
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
void RolloutBase::display(const scalar_array_t& timeTrajectory, const size_array_t& postEventIndicesStock,
                          const vector_array_t& stateTrajectory, const vector_array_t* const inputTrajectory) {
  std::cerr << "Trajectory length:      " << timeTrajectory.size() << '\n';
  std::cerr << "Total number of events: " << postEventIndicesStock.size() << '\n';
  if (!postEventIndicesStock.empty()) {
    std::cerr << "Event times: ";
    for (size_t ind : postEventIndicesStock) {
      std::cerr << timeTrajectory[ind] << ", ";
    }
    std::cerr << '\n';
  }
  std::cerr << '\n';

  const size_t numSubsystems = postEventIndicesStock.size() + 1;
  size_t k = 0;
  for (size_t i = 0; i < numSubsystems; i++) {
    for (; k < timeTrajectory.size(); k++) {
      std::cerr << "Index: " << k << '\n';
      std::cerr << "Time:  " << std::setprecision(12) << timeTrajectory[k] << '\n';
      std::cerr << "State: " << std::setprecision(3) << stateTrajectory[k].transpose() << '\n';
      if (inputTrajectory) {
        std::cerr << "Input: " << std::setprecision(3) << (*inputTrajectory)[k].transpose() << '\n';
      }

      if (i < postEventIndicesStock.size() && k + 1 == postEventIndicesStock[i]) {
        std::cerr << "+++ event took place +++" << '\n';
        k++;
        break;
      }
    }  // end of k loop
  }    // end of i loop
}

/******************************************************************************************************/
/******************************************************************************************************/
/******************************************************************************************************/
void RolloutBase::checkNumericalStability(ControllerBase* controller, const scalar_array_t& timeTrajectory,
                                          const size_array_t& postEventIndicesStock, const vector_array_t& stateTrajectory,
                                          const vector_array_t& inputTrajectory) const {
  if (!rolloutSettings_.checkNumericalStability_) {
    return;
  }

  for (size_t i = 0; i < timeTrajectory.size(); i++) {
    try {
      if (!stateTrajectory[i].allFinite()) {
        throw std::runtime_error("Rollout: state is not finite");
      }
      if (rolloutSettings_.reconstructInputTrajectory_ && !inputTrajectory[i].allFinite()) {
        throw std::runtime_error("Rollout: input is not finite");
      }
    } catch (const std::exception& error) {
      std::cerr << "what(): " << error.what() << " at time " + std::to_string(timeTrajectory[i]) + " [sec]." << '\n';

      // truncate trajectories
      scalar_array_t timeTrajectoryTemp;
      vector_array_t stateTrajectoryTemp;
      vector_array_t inputTrajectoryTemp;
      for (size_t j = 0; j <= i; j++) {
        timeTrajectoryTemp.push_back(timeTrajectory[j]);
        stateTrajectoryTemp.push_back(stateTrajectory[j]);
        if (rolloutSettings_.reconstructInputTrajectory_) {
          inputTrajectoryTemp.push_back(inputTrajectory[j]);
        }
      }

      // display
      const vector_array_t* const inputTrajectoryTempPtr = rolloutSettings_.reconstructInputTrajectory_ ? &inputTrajectoryTemp : nullptr;
      display(timeTrajectoryTemp, postEventIndicesStock, stateTrajectoryTemp, inputTrajectoryTempPtr);

      controller->display();

      throw;
    }
  }  // end of i loop
}
}  // namespace ocs2
