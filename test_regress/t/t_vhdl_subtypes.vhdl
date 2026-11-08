-- DESCRIPTION: Verilator: VHDL user-defined subtypes and array types
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

package types_pkg is
  subtype slv4 is std_logic_vector(3 downto 0);
  subtype small is integer range 0 to 7;
  type word_t is array (3 downto 0) of std_logic;
end;

library ieee;
use ieee.std_logic_1164.all;
use work.types_pkg.all;

entity t is
  port (
    a : in slv4;
    b : in word_t;
    n : in small;
    y : out slv4
  );
end;

architecture rtl of t is
begin
  y <= a;
end;
