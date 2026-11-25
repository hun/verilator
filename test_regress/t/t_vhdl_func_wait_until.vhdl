-- DESCRIPTION: Verilator: VHDL wait until on a function call that needs a temporary
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t_vhdl_func_wait_until is
  port (a  : in  std_logic_vector(7 downto 0);
        y1 : out std_logic_vector(7 downto 0);
        y2 : out std_logic_vector(7 downto 0));
end t_vhdl_func_wait_until;

architecture behav of t_vhdl_func_wait_until is
  function hash (x : std_logic_vector (7 downto 0)) return std_logic_vector is
    variable v : std_logic_vector (7 downto 0) := x;
  begin
    v := v xor std_logic_vector (shift_left (unsigned (v), 3));
    v := v xor std_logic_vector (shift_right (unsigned (v), 2));
    v := v xor std_logic_vector (shift_left (unsigned (v), 3));
    v := v xor std_logic_vector (shift_right (unsigned (v), 2));
    v := v xor std_logic_vector (shift_left (unsigned (v), 3));
    v := v xor std_logic_vector (shift_right (unsigned (v), 2));
    v := v xor std_logic_vector (shift_left (unsigned (v), 3));
    v := v xor std_logic_vector (shift_right (unsigned (v), 2));
    return v;
  end hash;
begin
  process
  begin
    -- The condition is evaluated again after every change of a
    wait until hash (a) (0) = '1';
    y1 <= hash (a);
    wait until hash (a) (0) = '0';
    y2 <= hash (a);
  end process;
end behav;
