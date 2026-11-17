-- DESCRIPTION: Verilator: unconstrained record port associated only by element
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

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a  : in std_logic_vector(3 downto 0);
    b  : in std_logic_vector(3 downto 0);
    ux : out std_logic_vector(3 downto 0);
    uy : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of t is
begin
  -- Only the elements are associated, so no actual fixes the record's member
  -- ranges and the unconstrained port cannot be specialized
  u : entity work.child_u
    port map (
      i.x => a,
      i.y => b,
      o.x => ux,
      o.y => uy
    );
end;
