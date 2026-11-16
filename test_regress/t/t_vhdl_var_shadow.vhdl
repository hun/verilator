-- DESCRIPTION: Verilator: a VHDL variable hides an enumeration literal of a used package
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

package codes_pkg is
  type code_t is (ra, rb);
end package codes_pkg;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
library work;
use work.codes_pkg.all;

entity t is
  port (
    o : out std_logic_vector(7 downto 0)
  );
end entity t;

architecture rtl of t is
begin
  process
    -- `ra` is also a literal of the used package, which this variable hides
    variable ra : std_logic_vector(7 downto 0);
  begin
    ra := std_logic_vector(to_unsigned(5, 8));
    o <= ra;
    wait;
  end process;
end architecture rtl;
