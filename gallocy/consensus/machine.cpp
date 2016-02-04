#include <unistd.h>
#include <pthread.h>

#include <cstdlib>
#include <ctime>
#include <iostream>

#include "gallocy/consensus/machine.h"
#include "gallocy/entrypoint.h"
#include "gallocy/models.h"
#include "gallocy/threads.h"
#include "gallocy/utils/http.h"
#include "gallocy/utils/logging.h"
#include "gallocy/utils/stringutils.h"
#include "restclient.h"  // NOLINT


void *GallocyMachine::work() {
  LOG_DEBUG("Starting HTTP client");

  if (gallocy_state == nullptr) {
    LOG_ERROR("The gallocy_state object is null.");
    abort();
  }

  gallocy_state->get_timer()->start();

  while (alive) {
    std::unique_lock<std::mutex> lk(gallocy_state->get_timer_mutex());
    // Wait here indefinitely until the alarm expires.
    gallocy_state->get_timer_cv().wait(lk);
    // If the timer expires the condition will be signaled, which indicates
    // that our leader failed to contact us in a timely manner. When this
    // happens, transition to a candidate state.
    if (gallocy_state->get_state() == RaftState::FOLLOWER) {
      gallocy_state->set_state(RaftState::CANDIDATE);
    }

    switch (gallocy_state->get_state()) {
      case RaftState::FOLLOWER:
        gallocy_state->set_state(state_candidate());
        break;
      case RaftState::LEADER:
        gallocy_state->set_state(state_leader());
        break;
      case RaftState::CANDIDATE:
        gallocy_state->set_state(state_candidate());
        break;
      default:
        LOG_ERROR("Client reached default handler.");
        break;
    }
  }
  return nullptr;
}


RaftState GallocyMachine::state_follower() {
  return RaftState::FOLLOWER;
}


RaftState GallocyMachine::state_leader() {
  uint64_t leader_term = gallocy_state->get_current_term();
  uint64_t leader_last_applied = gallocy_state->get_last_applied();
  uint64_t leader_commit_index = gallocy_state->get_commit_index();
  uint64_t leader_prev_log_index = gallocy_state->get_log()->get_previous_log_index();
  uint64_t leader_prev_log_term = gallocy_state->get_log()->get_previous_log_term();

  std::function<bool(const RestClient::Response &)> callback = [](const RestClient::Response &rsp) {
    if (rsp.code == 200) {
      gallocy::string body = rsp.body.c_str();
      gallocy::json response_json = gallocy::json::parse(body);
      bool success = response_json["success"];
      uint64_t supporter_term = response_json["term"];
      uint64_t local_term = gallocy_state->get_current_term();
      if (supporter_term > local_term) {
        gallocy_state->set_state(RaftState::FOLLOWER);
        gallocy_state->set_current_term(supporter_term);
      }
      return success;
    }
    return false;
  };

  gallocy::json j = {
    { "entries", gallocy::json::array() },
    { "leader_commit", leader_commit_index },
    { "previous_log_index", leader_prev_log_index },
    { "previous_log_term", leader_prev_log_term },
    { "term", leader_term },
  };

  // TODO(sholsapp): How we handle this is busted and needs to be refactored so
  // that the cv is usable here. This is also blocking, which is probably bad?
  uint64_t votes = utils::post_many("/raft/append_entries", config.peers, config.port, j.dump(), callback);
  // LOG_INFO("Received responses from " << votes << "/" << config.peers.size() << " peers");
  return RaftState::LEADER;
}


RaftState GallocyMachine::state_candidate() {
  if (gallocy_client->send_request_vote()) {
    gallocy_state->set_state(RaftState::LEADER);
    gallocy_state->get_timer()->reset();
    return state_leader();
  } else {
    gallocy_state->get_timer()->reset();
    gallocy_state->set_voted_for(0);
    return RaftState::CANDIDATE;
  }
}
