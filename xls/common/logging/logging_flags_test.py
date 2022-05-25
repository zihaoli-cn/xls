#
# Copyright 2020 The XLS Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Tests of logging flags."""

import subprocess

from xls.common import runfiles
from xls.common import test_base

LOGGER_PATH = runfiles.get_path('xls/common/logging/logger')


class LoggingFlagsTest(test_base.TestCase):

  def test_no_flags(self):
    comp = subprocess.run([LOGGER_PATH],
                          check=True,
                          encoding='utf-8',
                          stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE)
    self.assertEmpty(comp.stdout)
    self.assertNotIn('INFO message', comp.stderr)
    self.assertNotIn('WARNING message', comp.stderr)
    self.assertIn('ERROR message', comp.stderr)
    self.assertNotIn('XLS_VLOG', comp.stderr)

  def test_log_level_0(self):
    comp = subprocess.run([LOGGER_PATH, '--xls_stderrthreshold=0'],
                          check=True,
                          encoding='utf-8',
                          stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE)
    self.assertEmpty(comp.stdout)
    self.assertIn('INFO message', comp.stderr)
    self.assertIn('WARNING message', comp.stderr)
    self.assertIn('ERROR message', comp.stderr)
    self.assertNotIn('XLS_VLOG', comp.stderr)

  def test_log_level_1(self):
    comp = subprocess.run([LOGGER_PATH, '--xls_stderrthreshold=1'],
                          check=True,
                          encoding='utf-8',
                          stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE)
    self.assertEmpty(comp.stdout)
    self.assertNotIn('INFO message', comp.stderr)
    self.assertIn('WARNING message', comp.stderr)
    self.assertIn('ERROR message', comp.stderr)
    self.assertNotIn('XLS_VLOG', comp.stderr)

  def test_log_level_2(self):
    comp = subprocess.run([LOGGER_PATH, '--xls_stderrthreshold=2'],
                          check=True,
                          encoding='utf-8',
                          stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE)
    self.assertEmpty(comp.stdout)
    self.assertNotIn('INFO message', comp.stderr)
    self.assertNotIn('WARNING message', comp.stderr)
    self.assertIn('ERROR message', comp.stderr)
    self.assertNotIn('XLS_VLOG', comp.stderr)

  def test_vlog_level_1(self):
    # VLOG messages are logged at INFO level so stderr threshold must be 0.
    comp = subprocess.run([LOGGER_PATH, '--xls_stderrthreshold=0', '-v=1'],
                          check=True,
                          encoding='utf-8',
                          stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE)
    self.assertEmpty(comp.stdout)
    self.assertIn('INFO message', comp.stderr)
    self.assertIn('WARNING message', comp.stderr)
    self.assertIn('ERROR message', comp.stderr)
    self.assertIn('XLS_VLOG(1) message', comp.stderr)
    self.assertNotIn('XLS_VLOG(2) message', comp.stderr)
    self.assertIn('XLS_VLOG_IS_ON(1)', comp.stderr)
    self.assertNotIn('XLS_VLOG_IS_ON(2)', comp.stderr)

  def test_vlog_level_2(self):
    # VLOG messages are logged at INFO level so stderr threshold must be 0.
    comp = subprocess.run([LOGGER_PATH, '--xls_stderrthreshold=0', '-v=2'],
                          check=True,
                          encoding='utf-8',
                          stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE)
    self.assertEmpty(comp.stdout)
    self.assertIn('INFO message', comp.stderr)
    self.assertIn('WARNING message', comp.stderr)
    self.assertIn('ERROR message', comp.stderr)
    self.assertIn('XLS_VLOG(1) message', comp.stderr)
    self.assertIn('XLS_VLOG(2) message', comp.stderr)
    self.assertIn('XLS_VLOG_IS_ON(1)', comp.stderr)
    self.assertIn('XLS_VLOG_IS_ON(2)', comp.stderr)

  def test_vlog_level_2_but_stderrthreshold_too_high(self):
    # If xls_stderrthreshold is not zero VLOG messages do not appear.
    comp = subprocess.run([LOGGER_PATH, '--xls_stderrthreshold=1', '-v=2'],
                          check=True,
                          encoding='utf-8',
                          stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE)
    self.assertEmpty(comp.stdout)
    self.assertNotIn('INFO message', comp.stderr)
    self.assertIn('WARNING message', comp.stderr)
    self.assertIn('ERROR message', comp.stderr)
    self.assertNotIn('XLS_VLOG', comp.stderr)

  def test_vmodule_matches_filename_level_1(self):
    # VLOG messages are logged at INFO level so stderr threshold must be 0.
    comp = subprocess.run(
        [LOGGER_PATH, '--xls_stderrthreshold=0', '--vmodule=logger=1'],
        check=True,
        encoding='utf-8',
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE)
    self.assertEmpty(comp.stdout)
    self.assertIn('INFO message', comp.stderr)
    self.assertIn('WARNING message', comp.stderr)
    self.assertIn('ERROR message', comp.stderr)
    self.assertIn('XLS_VLOG(1) message', comp.stderr)
    self.assertNotIn('XLS_VLOG(2) message', comp.stderr)
    self.assertIn('XLS_VLOG_IS_ON(1)', comp.stderr)
    self.assertNotIn('XLS_VLOG_IS_ON(2)', comp.stderr)

  def test_vmodule_matches_filename_level_2(self):
    # VLOG messages are logged at INFO level so stderr threshold must be 0.
    comp = subprocess.run(
        [LOGGER_PATH, '--xls_stderrthreshold=0', '--vmodule=logger=2'],
        check=True,
        encoding='utf-8',
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE)
    self.assertEmpty(comp.stdout)
    self.assertIn('INFO message', comp.stderr)
    self.assertIn('WARNING message', comp.stderr)
    self.assertIn('ERROR message', comp.stderr)
    self.assertIn('XLS_VLOG(1) message', comp.stderr)
    self.assertIn('XLS_VLOG(2) message', comp.stderr)
    self.assertIn('XLS_VLOG_IS_ON(1)', comp.stderr)
    self.assertIn('XLS_VLOG_IS_ON(2)', comp.stderr)

  def test_vmodule_not_matches_filename(self):
    # VLOG messages are logged at INFO level so stderr threshold must be 0.
    comp = subprocess.run(
        [LOGGER_PATH, '--xls_stderrthreshold=0', '--vmodule=foobar=2'],
        check=True,
        encoding='utf-8',
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE)
    self.assertEmpty(comp.stdout)
    self.assertIn('INFO message', comp.stderr)
    self.assertIn('WARNING message', comp.stderr)
    self.assertIn('ERROR message', comp.stderr)
    self.assertNotIn('XLS_VLOG', comp.stderr)


if __name__ == '__main__':
  test_base.main()
