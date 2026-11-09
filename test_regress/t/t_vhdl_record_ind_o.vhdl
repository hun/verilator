-- DESCRIPTION: Verilator: VHDL record child for the artificial-open test
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

package recpko is
  type pair_o is record
    a : std_logic_vector;
    b : std_logic_vector;
  end record;
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.recpko.all;

-- Port `a` shares its name with record member `i.a`; the unassociated port must
-- still be recognized as an open of `a`, not treated as already associated
entity child_o is
  port (
    i : in pair_o;
    a : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of child_o is
begin
  a <= i.b;
end;
