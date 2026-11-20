-- DESCRIPTION: Verilator: VHDL array of unconstrained vectors, constrained at use
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- `array (natural range <>) of std_logic_vector` leaves the element range open; a use
-- constrains both the index and the element: `m(0 to 1)(3 downto 0)`. Access stays
-- chained (`m(i)(j)`), as the object is a one-dimensional array of vectors.

library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_array_of_vec is
  port (
    a : in  std_logic_vector(7 downto 0);
    o : out std_logic_vector(3 downto 0);
    d : out std_logic_vector(3 downto 0);
    b : out std_logic;
    e : out std_logic
  );
end entity;

architecture a of t_vhdl_array_of_vec is
  type tvec is array (natural range <>) of std_logic_vector;
  type tmat is array (natural range <>) of tvec;
  signal m  : tvec(0 to 1)(3 downto 0);
  signal mm : tmat(0 to 1)(0 to 1)(3 downto 0);
begin
  m(0)(3 downto 0)  <= a(7 downto 4);
  m(1)(3 downto 0)  <= a(3 downto 0);
  mm(0)(0)(3 downto 0) <= a(7 downto 4);
  mm(1)(1)(3 downto 0) <= a(3 downto 0);
  o                 <= m(1)(3 downto 0);
  d                 <= m(0);
  b                 <= m(0)(0);
  e                 <= mm(1)(1)(2);
end architecture;
