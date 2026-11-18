-- DESCRIPTION: Verilator: a case over std_logic literals that the two-state model merges
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- open-logic's to01() chooses '0' or 'L' in one alternative and '1' or 'H' in another.
-- The nine-valued std_logic is modelled with two states, so those pairs name one value
-- each; that is a single choice, not the duplicate the standard rejects. The selector
-- must not be static, or the case folds before its choices are collected.

library ieee;
use ieee.std_logic_1164.all;

package pk is
  function to01x (a : std_logic) return std_logic;
  function to01x (a : std_logic_vector) return std_logic_vector;
end package;

package body pk is
  function to01x (a : std_logic) return std_logic is
  begin
    case a is
      when '0' | 'L' => return '0';
      when '1' | 'H' => return '1';
      when others => return '0';
    end case;
  end function;

  function to01x (a : std_logic_vector) return std_logic_vector is
    variable result_v : std_logic_vector(a'range);
  begin
    for i in a'range loop
      result_v(i) := to01x(a(i));
    end loop;
    return result_v;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use work.pk.all;

entity t is
  port (
    a : in  std_logic;
    v : in  std_logic_vector(3 downto 0);
    o : out std_logic;
    w : out std_logic_vector(3 downto 0)
  );
end entity;

architecture rtl of t is
begin
  o <= to01x(a);
  w <= to01x(v);
end architecture;
