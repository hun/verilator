-- DESCRIPTION: Verilator: VHDL unresolved signals have one driver
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- An unresolved signal may have only one driver (IEEE 1076-2008 6.4.2.3); GHDL
-- refuses this design at elaboration.

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a : in  bit;
    b : in  bit;
    y : out bit;
    y2 : out std_logic
  );
end;

architecture rtl of t is
  signal s : bit;  -- `bit` has no resolution function
  signal u : std_ulogic;  -- `std_ulogic` is unresolved; `std_logic` is not
begin
  s <= a;
  s <= b;
  u <= '0';
  u <= '1';
  y <= s;
  y2 <= u;
end;
