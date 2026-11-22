/*
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "berberis/guest_os_primitives/guest_thread.h"

#include <pthread.h>
#include <semaphore.h>

#include "berberis/base/config_globals.h"
#include "berberis/guest_state/guest_addr.h"

#include "guest_thread_pthread_create.h"
#include "scoped_signal_blocker.h"

namespace berberis {

int CreateNewGuestThread(pthread_t* thread_id,
                         const pthread_attr_t* attr,
                         void* guest_stack,
                         size_t guest_stack_size,
                         size_t guest_guard_size,
                         GuestAddr func,
                         GuestAddr arg) {
  GuestThreadCreateInfo info;
  info.thread = GuestThread::CreatePthread(guest_stack, guest_stack_size, guest_guard_size);
  if (info.thread == nullptr) {
    return EAGAIN;
  }
  info.func = func;
  info.arg = arg;
  sem_init(&info.sem, 0, 0);
  // region digitalis
  if (IsConfigFlagSet(kGlibcHostThreadIdHandoff)) {
    int detach_state = PTHREAD_CREATE_JOINABLE;
    if (attr != nullptr) {
      pthread_attr_getdetachstate(attr, &detach_state);
    }
    if (detach_state == PTHREAD_CREATE_JOINABLE) {
      info.thread->SetJoinableGuestPthread();
    }
  }
  // endregion

  int res;
  {
    ScopedSignalBlocker signal_blocker;
    info.mask = *signal_blocker.old_mask();
    res = pthread_create(thread_id, attr, RunGuestThread, &info);
    if (res == 0) {
      CHECK_EQ(0, sem_wait(&info.sem));  // Wait with blocked signals to avoid EINTR.
      // region digitalis
      // The guest's pthread_t is the record that pthread_self() returns in the new thread.
      if (IsConfigFlagSet(kGlibcHostThreadIdHandoff)) {
        *thread_id = reinterpret_cast<pthread_t>(info.guest_pthread);
      }
      // endregion
    }
  }

  if (res != 0) {
    GuestThread::Destroy(info.thread);
  }

  sem_destroy(&info.sem);
  return res;
}

}  // namespace berberis
