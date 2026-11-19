-- DESCRIPTION: Verilator: string constants wider than 32 bits, and a character literal
-- in a concatenation
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- poc's configuration keeps a 32-character device string and builds others by
-- concatenating a character literal with a character constant. A fold over a string is
-- speculative and must not read the value as a 32-bit integer, and a character literal
-- beside a character is a character of the string, not a logic element.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package widestr is
  constant mark_c : character := '!';
  constant wide_c : string := "0123456789ABCDEF0123456789ABCDEF";
  constant pair_c : string := '3' & mark_c;

  function len(s : string) return natural;

  constant wide_len : natural := len(wide_c);
  constant pair_len : natural := len(pair_c);
end package;

package body widestr is
  function len(s : string) return natural is
    variable n : natural := 0;
  begin
    for i in s'range loop
      n := n + 1;
    end loop;
    return n;
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.widestr.all;

entity t is
  port (
    clk   : in  std_logic;
    o_len : out std_logic_vector(7 downto 0);
    o_pair : out std_logic_vector(15 downto 0)
  );
end entity;

architecture rtl of t is
begin
  process (clk)
  begin
    if rising_edge(clk) then
      o_len <= std_logic_vector(to_unsigned(wide_len, 8));
      o_pair <= std_logic_vector(to_unsigned(character'pos(pair_c(1)), 8))
                & std_logic_vector(to_unsigned(character'pos(pair_c(2)), 8));
    end if;
  end process;
end architecture;
