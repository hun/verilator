-- DESCRIPTION: Verilator: VHDL context declaration imports its packages
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

-- A context declaration can reference another context and bundle packages
context inner_ctx is
  library ieee;
  use ieee.numeric_std.all;
end context;

context outer_ctx is
  context work.inner_ctx;
  use work.ctx_pkg.all;
end context;

package ctx_pkg is
  constant WIDTH : natural := 4;
end package;
