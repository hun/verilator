-- DESCRIPTION: Verilator: a type attribute of a variable assigned from a call
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- A process variable tracks the value written to it, but `v'high` names the
-- variable's declared type, so it must still resolve after `v := f(x)` (open-logic's
-- priority arbiter slices its prefix-OR result this way).

library ieee;
use ieee.std_logic_1164.all;

package p is
  function prefix_or(v : std_logic_vector) return std_logic_vector;
end package;

package body p is
  function prefix_or(v : std_logic_vector) return std_logic_vector is
    variable acc : std_logic_vector(v'length - 1 downto 0) := (others => '0');
    variable run : std_logic := '0';
  begin
    for i in v'range loop
      run := run or v(i);
      acc(i) := run;
    end loop;
    return acc;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use work.p.all;

entity t is
  port (i : in std_logic_vector(7 downto 0); o : out std_logic_vector(7 downto 0));
end entity;

architecture rtl of t is
begin
  q : process (all)
    variable v : std_logic_vector(7 downto 0);
  begin
    v := prefix_or(i);
    o <= v and not ('0' & v(v'high downto 1));
  end process;
end architecture;
