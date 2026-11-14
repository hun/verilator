-- DESCRIPTION: Verilator: an attribute of a type mark with an argument folds in a value path
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    o_pos  : out integer;
    o_size : out integer
  );
end entity t;

architecture rtl of t is
  -- The same expressions fold in a constant; they must fold in an assignment too
  constant folded_c : integer := natural'pos(3);
begin
  o_pos  <= natural'pos(3);
  o_size <= natural'pos(folded_c + 2);
end architecture rtl;
