-- DESCRIPTION: Verilator: a function case without 'others' folded statically
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

package p is
  function arms(x : integer) return integer;
end package p;

package body p is
  function arms(x : integer) return integer is
  begin
    case x is
      when 0 => return 1;
      when 1 => return 2;
    end case;
  end function arms;
end package body p;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.p.all;

entity t is
  port (
    n : in  integer range 0 to 1;
    o : out std_logic_vector(3 downto 0)
  );
end entity t;

architecture rtl of t is
begin
  -- The actual comes from a signal, so the fold inlines the body but cannot tell which
  -- arm of the case runs, and there is no 'others' to fall back on
  o <= std_logic_vector(to_unsigned(arms(n), 4));
end architecture rtl;
