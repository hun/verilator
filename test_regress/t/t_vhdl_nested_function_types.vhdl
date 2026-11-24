-- DESCRIPTION: Verilator: nested functions and null-array concatenation
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (o : out std_logic_vector(7 downto 0));
end entity t;

architecture rtl of t is
  type bits_t is array (natural range <>) of bit_vector(1 downto 0);

  -- The inner function names a type of the architecture in its formal and its result,
  -- and reads a local of the outer one
  function outer return bits_t is
    variable base : bits_t(0 to 1) := ("01", "10");
    function inner(p : bits_t; n : natural) return bits_t is
      variable r : bits_t(p'range) := p;
    begin
      r(n) := base(1);
      return r;
    end function;
  begin
    return inner(base, 0);
  end function;

  function one return bits_t is
    variable r : bits_t(0 to 0) := (0 => "11");
  begin
    return r;
  end function;

  -- A null array concatenated with a value is the value (IEEE 1076-2008 9.2.5)
  function joined return bits_t is
    variable empty : bits_t(0 to -1);
  begin
    return empty & one;
  end function;

  function joined_right return bits_t is
    variable empty : bits_t(0 to -1);
  begin
    return one & empty;
  end function;

  constant c : bits_t := outer;
  constant j : bits_t := joined;
  constant k : bits_t := joined_right;
begin
  o(0) <= '1' when c(0) = "10" else '0';
  o(1) <= '1' when c(1) = "10" else '0';
  o(2) <= '1' when j'length = 1 else '0';
  o(3) <= '1' when j(0) = "11" else '0';
  o(4) <= '1' when k'length = 1 else '0';
  o(5) <= '1' when k(0) = "11" else '0';
  o(7 downto 6) <= "11";
end architecture rtl;
