-- DESCRIPTION: Verilator: an arithmetic result assigned to a std_logic_vector
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;

entity t is
  port (a : in std_logic_vector(3 downto 0);
        b : in std_logic_vector(2 downto 0);
        sum : out std_logic_vector(3 downto 0);
        prod : out std_logic_vector(6 downto 0);
        mixed : out std_logic_vector(6 downto 0);
        plus : out std_logic_vector(3 downto 0);
        neg : out std_logic_vector(3 downto 0);
        magnitude : out std_logic_vector(3 downto 0));
end entity t;

architecture rtl of t is
begin
  -- std_logic_arith has each operator with a std_logic_vector result too, which the target
  -- picks over the unsigned one
  sum <= unsigned(a) + unsigned(b);
  prod <= unsigned(a) * unsigned(b);
  -- Unsigned times signed is signed, as wide as both operands together
  mixed <= unsigned(a) * signed(b);
  -- The unary operators have both results as well
  plus <= +unsigned(a);
  neg <= -signed(a);
  magnitude <= abs signed(a);
end architecture rtl;
