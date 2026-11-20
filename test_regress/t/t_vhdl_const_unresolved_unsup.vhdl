-- DESCRIPTION: Verilator: an array constant of records with unconstrained members
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

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
