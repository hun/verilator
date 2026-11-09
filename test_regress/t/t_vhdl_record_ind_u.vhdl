-- DESCRIPTION: Verilator: VHDL unconstrained record child for the element test
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

package recpku is
  type pair_u is record
    x : std_logic_vector;
    y : std_logic_vector;
  end record;
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.recpku.all;

entity child_u is
  port (
    i : in pair_u;
    o : out pair_u
  );
end;

architecture rtl of child_u is
begin
  o.x <= i.x;
  o.y <= i.y;
end;
