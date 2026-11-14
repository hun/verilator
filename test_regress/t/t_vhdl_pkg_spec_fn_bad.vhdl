-- DESCRIPTION: Verilator: a package specification constant initialized by a body function
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

package pkg is
  constant picked : integer := first_length(5, 2);
  function first_length(a, b : integer) return integer;
end package pkg;

package body pkg is
  function first_length(a, b : integer) return integer is
  begin
    return a + b;
  end function first_length;
end package body pkg;

library ieee;
use ieee.std_logic_1164.all;

library work;
use work.pkg.all;

entity t is
  port (o : out integer);
end entity t;

architecture rtl of t is
begin
  o <= picked;
end architecture rtl;
