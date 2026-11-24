-- DESCRIPTION: Verilator: VHDL an array index range written with attributes
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t_vhdl_array_attr_range is
  port (sel   : in  std_logic_vector(1 downto 0);
        q_up  : out std_logic_vector(7 downto 0);
        q_dn  : out std_logic_vector(7 downto 0);
        q_rev : out std_logic_vector(7 downto 0);
        q_rng : out std_logic_vector(7 downto 0));
end t_vhdl_array_attr_range;

architecture behav of t_vhdl_array_attr_range is
  subtype idx_t is std_logic_vector(3 downto 0);
  subtype byte_t is std_logic_vector(7 downto 0);
  type up_t is array (idx_t'low to idx_t'high) of byte_t;
  type dn_t is array (idx_t'high downto idx_t'low) of byte_t;
  type rev_t is array (idx_t'reverse_range) of byte_t;
  type rng_t is array (idx_t'range) of byte_t;

  -- The first element of an aggregate takes the left bound of the index range
  constant c_up  : up_t  := (x"10", x"11", x"12", x"13");
  constant c_dn  : dn_t  := (x"20", x"21", x"22", x"23");
  constant c_rev : rev_t := (x"30", x"31", x"32", x"33");
  constant c_rng : rng_t := (x"40", x"41", x"42", x"43");
begin
  q_up <= c_up (to_integer (unsigned (sel)));
  q_dn <= c_dn (to_integer (unsigned (sel)));
  q_rev <= c_rev (to_integer (unsigned (sel)));
  q_rng <= c_rng (to_integer (unsigned (sel)));
end behav;
