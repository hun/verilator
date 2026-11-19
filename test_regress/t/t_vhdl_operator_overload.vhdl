-- -*- mode: verilog -*-
-- DESCRIPTION: Verilator: a user operator over another type leaves the built-in one
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- A user-defined "xor" over T_POLARITY (poc's utils.vhdl) must not be selected for
-- `std_logic_vector(z(...)) xor b`, whose operands are both std_logic_vector. The
-- conversion carries its mark's type, so overload resolution sees a std_logic_vector
-- and keeps the built-in elementwise operator.

library ieee;
use ieee.std_logic_1164.all;

package pol_pkg is
  type t_polarity is (high_active, low_active);
  function "xor"(left : t_polarity; right : std_logic_vector) return std_logic_vector;
  function "xor"(left : std_logic_vector; right : t_polarity) return std_logic_vector;
end package pol_pkg;

package body pol_pkg is
  function "xor"(left : t_polarity; right : std_logic_vector) return std_logic_vector is
  begin
    if left = high_active then
      return right;
    else
      return not right;
    end if;
  end function "xor";

  function "xor"(left : std_logic_vector; right : t_polarity) return std_logic_vector is
  begin
    if right = high_active then
      return left;
    else
      return not left;
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
  constant pol : t_polarity := low_active;
begin
  z <= unsigned(a & "00") + unsigned(b);

  -- Built-in: both operands are std_logic_vector once the conversion has its type
  o_left <= std_logic_vector(z(3 downto 0)) xor b;

  -- The same with the conversion on the right side
  o_right <= b xor std_logic_vector(z(3 downto 0));

  -- The user operator is still selected where its formal type matches
  o_pol <= pol xor b;
end architecture rtl;
