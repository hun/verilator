-- DESCRIPTION: Verilator: an unconstrained multi-dimensional port is refused cleanly
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

package multidim_pkg is
  type arr2d is array (integer range <>, integer range <>) of bit;
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.multidim_pkg.all;

entity t is
  port (
    c : in  arr2d;
    o : out bit
  );
end entity;

architecture rtl of t is
begin
  o <= '0';
end architecture;
