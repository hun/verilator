-- DESCRIPTION: Verilator: VHDL qualified package type reference
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

package defs is
  type rec is record
    a : std_logic_vector(3 downto 0);
    b : std_logic_vector(3 downto 0);
  end record;
end package;

library ieee;
use ieee.std_logic_1164.all;

entity dut is
  port (ri : in work.defs.rec;
        o  : out std_logic_vector(3 downto 0));
end entity;

architecture rtl of dut is
begin
  o <= ri.a and ri.b;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (ia : in  std_logic_vector(3 downto 0);
        ib : in  std_logic_vector(3 downto 0);
        o  : out std_logic_vector(3 downto 0));
end entity;

architecture rtl of t is
  signal ri : work.defs.rec;
begin
  ri.a <= ia;
  ri.b <= ib;
  u : entity work.dut
    port map (ri => ri, o => o);
end architecture;
