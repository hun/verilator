-- DESCRIPTION: Verilator: an array constant of records with unconstrained members
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- PoC's configuration package holds arrays of board descriptions, records whose members
-- are strings and vectors. The element type leaves those members unconstrained, so the
-- array constant has no width until its aggregate supplies one, and the frontend reports
-- that instead of taking the width from the first element.

package p is
  type t_rec is record
    a : std_logic_vector;
    b : string;
  end record;
  type t_arr is array (natural range <>) of t_rec;
  constant c : t_arr := (
    (a => "00", b => "ab"),
    (a => "01", b => "cd"));
end package;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (o : out std_logic);
end entity;

architecture rtl of t is
begin
  o <= '0';
end architecture;
