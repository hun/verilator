-- DESCRIPTION: Verilator: dynamic VHDL enumeration succ bound check
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

package enum_dyn_pkg is
  type state_t is (idle, run, stop);
end package;

use work.enum_dyn_pkg.all;

entity t is
  port (
    e : in  state_t;
    s : out state_t
  );
end;

architecture rtl of t is
begin
  s <= state_t'succ(e);
end;
