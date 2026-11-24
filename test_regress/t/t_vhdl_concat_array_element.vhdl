-- DESCRIPTION: Verilator: VHDL an array element that is itself an array joined to an array
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_concat_array_element is
  port (i : in  std_logic_vector(23 downto 0);
        o : out std_logic_vector(23 downto 0));
end t_vhdl_concat_array_element;

architecture behav of t_vhdl_concat_array_element is
  subtype byte_t is std_logic_vector(7 downto 0);
  type bytes_t is array (natural range <>) of byte_t;
  subtype row_t is bytes_t(0 to 2);
  signal r_in, r_out : row_t;
begin
  r_in (0) <= i (7 downto 0);
  r_in (1) <= i (15 downto 8);
  r_in (2) <= i (23 downto 16);
  -- A byte is an element of bytes_t, though it is an array itself: rotate the row
  r_out <= r_in (r_in'right) & r_in (r_in'left to r_in'right - 1);
  o <= r_out (2) & r_out (1) & r_out (0);
end behav;
