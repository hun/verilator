-- DESCRIPTION: Verilator: an array constant of records with unconstrained members
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- An array constant whose element type leaves a member unconstrained has no width until
-- its aggregate supplies one; the frontend reports that rather than building a type of
-- zero width from it. (An array whose elements are fully constrained does take its bounds
-- from the aggregate: see t_vhdl_array_const.)

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
