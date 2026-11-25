/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "gtest/gtest.h"

#include <pthread.h>
#include <sys/types.h>  // pid_t

#include "berberis/base/config_globals.h"
#include "berberis/base/gettid.h"
#include "berberis/guest_os_primitives/guest_thread.h"
#include "berberis/guest_os_primitives/guest_thread_manager.h"

#include "guest_thread_manager_impl.h"  // AttachCurrentThread, DetachCurrentThread
#include "guest_thread_map.h"

namespace berberis {

namespace {

// region digitalis
// b/280671643: a pthread key created after the guest thread key gets its destructor called after
// GuestThreadDtor in the same round. The guest thread must still be attached then.

pthread_key_t g_late_key;
GuestThread* g_thread_in_body = nullptr;
GuestThread* g_thread_in_late_dtor = nullptr;
bool g_attached_in_late_dtor = true;
pid_t g_tid = 0;

void LateKeyDtor(void* /* value */) {
  bool attached;
  g_thread_in_late_dtor = AttachCurrentThread(false, &attached);
  g_attached_in_late_dtor = attached;
  if (attached) {
    DetachCurrentThread();
  }
}

void* LateKeyThreadFunc(void* /* arg */) {
  g_tid = GettidSyscall();
  g_thread_in_body = GetCurrentGuestThread();
  pthread_setspecific(g_late_key, &g_late_key);
  return nullptr;
}

TEST(GuestThreadManager, StaysAttachedForLaterKeyDestructors) {
  ASSERT_EQ(0, pthread_key_create(&g_late_key, LateKeyDtor));
  pthread_t thread;
  ASSERT_EQ(0, pthread_create(&thread, nullptr, LateKeyThreadFunc, nullptr));
  ASSERT_EQ(0, pthread_join(thread, nullptr));
  ASSERT_EQ(0, pthread_key_delete(g_late_key));

  ASSERT_NE(nullptr, g_thread_in_body);
  EXPECT_FALSE(g_attached_in_late_dtor);
  EXPECT_EQ(g_thread_in_body, g_thread_in_late_dtor);
  // Detached on the last destructor round all the same.
  EXPECT_EQ(nullptr, GuestThreadMap::GetInstance()->FindThread(g_tid));
}

// With glibc-host-thread-id-handoff the guest's pthread_t (the record) must stay the same for a
// host thread that gets a new GuestThread after a detach (Boehm GC looks threads up by
// pthread_self()). Run with BERBERIS_FLAGS=glibc-host-thread-id-handoff and a gtest filter on
// this test only: the flag is never set on a real bionic host.

void* RecordThreadFunc(void* arg) {
  auto* records = static_cast<void**>(arg);
  bool attached;
  GuestThread* thread = AttachCurrentThread(false, &attached);
  records[0] = thread->GetGlibcHostPthreadRecord();
  DetachCurrentThread();
  thread = AttachCurrentThread(false, &attached);
  records[1] = thread->GetGlibcHostPthreadRecord();
  DetachCurrentThread();
  return nullptr;
}

TEST(GuestThreadManager, GlibcHostPthreadRecordSurvivesReattach) {
#if !defined(__BIONIC__)
  GTEST_SKIP() << "Records are mapped by bionic builds of Berberis only";
#endif
  if (!IsConfigFlagSet(kGlibcHostThreadIdHandoff)) {
    GTEST_SKIP() << "Needs BERBERIS_FLAGS=glibc-host-thread-id-handoff";
  }
  void* first[2] = {};
  void* second[2] = {};
  pthread_t thread;
  ASSERT_EQ(0, pthread_create(&thread, nullptr, RecordThreadFunc, first));
  ASSERT_EQ(0, pthread_join(thread, nullptr));
  ASSERT_EQ(0, pthread_create(&thread, nullptr, RecordThreadFunc, second));
  ASSERT_EQ(0, pthread_join(thread, nullptr));

  ASSERT_NE(nullptr, first[0]);
  EXPECT_EQ(first[0], first[1]);
  EXPECT_EQ(second[0], second[1]);
}
// endregion

}  // namespace

}  // namespace berberis
