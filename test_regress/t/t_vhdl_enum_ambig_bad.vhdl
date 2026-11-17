-- DESCRIPTION: Verilator: an enumeration literal shared by two types without a context is ambiguous
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

package enum_ambig_pkg is
  type a_t is (a0, shared);
  type b_t is (shared, b1);
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.enum_ambig_pkg.all;

entity t is
  port (o : out bit);
end entity;

architecture rtl of t is
begin
  -- No expected type selects one of the two visible 'shared' literals
  o <= '1' when shared = shared else '0';
end architecture;
