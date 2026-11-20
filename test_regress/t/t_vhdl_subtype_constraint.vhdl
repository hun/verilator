-- DESCRIPTION: Verilator: a discrete subtype as an array index constraint
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- open-logic sizes its packet FIFO's pointers with `unsigned(Addr_c)`, where Addr_c is a
-- subtype of integer. The subtype's range is the vector's index range.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    a : out std_logic_vector(3 downto 0);
    b : out unsigned(3 downto 0);
    c : out std_logic_vector(1 to 2)
  );
end entity;

architecture rtl of t is
  subtype R_c is integer range 3 downto 0;
  subtype Up_c is integer range 1 to 2;
  constant C1_c : std_logic_vector(R_c) := "1010";
  signal S : unsigned(R_c) := "0101";
  constant C2_c : std_logic_vector(Up_c) := "11";
begin
  a <= C1_c;
  b <= S;
  c <= C2_c;
end architecture;
