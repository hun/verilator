-- DESCRIPTION: Verilator: VHDL deferred constant completed in a package body
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

package deferred_pkg is
  -- Completed in the package body by a function call
  constant INTEGER_BITS : positive;
  function get_integer_bits return positive;
  function log2ceil(arg : positive) return natural;
end package deferred_pkg;

package body deferred_pkg is
  function get_integer_bits return positive is
  begin
    return 32;
  end function;

  constant INTEGER_BITS : positive := get_integer_bits;

  function log2ceil(arg : positive) return natural is
  begin
    if arg = 1 then
      return 0;
    end if;
    for i in 1 to INTEGER_BITS - 2 loop
      if arg <= 2 ** i then
        return i;
      end if;
    end loop;
    return INTEGER_BITS - 1;
  end function;
end package body deferred_pkg;


library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.deferred_pkg.all;

entity t is
  port (
    len_o : out integer;
    q     : out signed(3 downto 0)
  );
end entity t;

architecture rtl of t is
  constant E : natural := log2ceil(13);
  signal s : signed(E - 1 downto 0) := (others => '0');
begin
  len_o <= E;
  q     <= s;
end architecture rtl;
