-- -*- mode: verilog -*-
-- DESCRIPTION: Verilator: a user operator over another type leaves the built-in one
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A user-defined "xor" over edge_sense (poc's utils.vhdl) must not be selected for
-- `std_logic_vector(z(...)) xor b`, whose operands are both std_logic_vector. The
-- conversion carries its mark's type, so overload resolution sees a std_logic_vector
-- and keeps the built-in elementwise operator.

library ieee;
use ieee.std_logic_1164.all;

package pol_pkg is
  type edge_sense is (sense_hi, sense_lo);
  function "xor"(lhs : edge_sense; rhs : std_logic_vector) return std_logic_vector;
  function "xor"(lhs : std_logic_vector; rhs : edge_sense) return std_logic_vector;
end package pol_pkg;

package body pol_pkg is
  function "xor"(lhs : edge_sense; rhs : std_logic_vector) return std_logic_vector is
  begin
    if lhs = sense_hi then
      return rhs;
    else
      return not rhs;
    end if;
  end function "xor";

  function "xor"(lhs : std_logic_vector; rhs : edge_sense) return std_logic_vector is
  begin
    if rhs = sense_hi then
      return lhs;
    else
      return not lhs;
    end if;
  end function "xor";
end package body pol_pkg;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.pol_pkg.all;

entity t is
  port(
    a : in std_logic_vector(3 downto 0);
    b : in std_logic_vector(3 downto 0);
    o_left : out std_logic_vector(3 downto 0);
    o_right : out std_logic_vector(3 downto 0);
    o_pol : out std_logic_vector(3 downto 0)
  );
end entity t;

architecture rtl of t is
  signal z : unsigned(5 downto 0);
  constant pol : edge_sense := sense_lo;
begin
  z <= unsigned(a & "00") + unsigned(b);

  -- Built-in: both operands are std_logic_vector once the conversion has its type
  o_left <= std_logic_vector(z(3 downto 0)) xor b;

  -- The same with the conversion on the right side
  o_right <= b xor std_logic_vector(z(3 downto 0));

  -- The user operator is still selected where its formal type matches
  o_pol <= pol xor b;
end architecture rtl;
