-- DESCRIPTION: Verilator: a function body that rewrites an array local too often
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- An element write rebuilds the local's whole value, and each read of the local
-- copies that value into the rebuild, so a body that reads the local back grows
-- its expression by a factor per write. The fold stops rather than exhausting
-- memory.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package p is
  function heavy(v : unsigned) return unsigned;
end package;

package body p is
  function heavy(v : unsigned) return unsigned is
    type arr_t is array (natural range <>) of unsigned(31 downto 0);
    variable a : arr_t(0 to 1) := (others => (others => '0'));
  begin
    a(0) := resize(v, 32);
    for i in 0 to 31 loop
      a(1)(i) := a(0)(i) or a(0)((i / 2 + 1) mod 32);
      a(0)(i) := a(1)(i) xor a(0)((i + 3) mod 32);
    end loop;
    return a(1);
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.p.all;

entity t is
  port (i : in unsigned(7 downto 0); o : out unsigned(31 downto 0));
end entity;

architecture rtl of t is
begin
  o <= heavy(i);
end architecture;
