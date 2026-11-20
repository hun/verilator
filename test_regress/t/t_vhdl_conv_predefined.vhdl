-- DESCRIPTION: Verilator: conversions to the predefined integer subtypes and to
--              numeric_std's unresolved vectors
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    o_nat : out std_logic_vector(3 downto 0);
    o_pos : out std_logic_vector(3 downto 0);
    o_uu  : out std_logic_vector(3 downto 0);
    o_us  : out std_logic_vector(3 downto 0)
  );
end entity t;

architecture rtl of t is
  signal raw : std_logic_vector(3 downto 0) := "1011";
  -- The integer subtypes are conversions of an integer, the unresolved vectors
  constant nat : natural  := natural(7);
  constant pos : positive := positive(3);
  -- A signal converts a vector at run time, where the operand's value is known
  signal uu : u_unsigned(3 downto 0) := u_unsigned(raw);
  signal us : u_signed(3 downto 0) := u_signed(raw);
begin
  o_nat <= std_logic_vector(to_unsigned(nat, 4));
  o_pos <= std_logic_vector(to_unsigned(pos, 4));
  o_uu  <= std_logic_vector(uu);
  o_us  <= std_logic_vector(us);
end architecture rtl;