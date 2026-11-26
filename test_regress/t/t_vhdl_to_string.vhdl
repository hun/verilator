-- DESCRIPTION: Verilator: the to_string family of functions
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
  generic (n : natural := 42);
  port (v : in std_logic_vector(5 downto 0);
        s : in signed(4 downto 0);
        u : in unsigned(2 downto 0);
        b : in std_logic;
        o : out std_logic_vector(7 downto 0));
end entity t;

architecture rtl of t is
  -- A static value gives a constant string, IEEE 1076-2008 5.2.6
  constant int_text : string := to_string(n);
  constant hex_text : string := to_hstring(x"A5");
  constant flag : boolean := n > 3;
  constant bool_text : string := to_string(flag);
begin
  -- A vector of known width gives a string of known length: one character per bit, per
  -- three bits, or per four, a signed vector extending by its sign
  o(0) <= '1' when int_text = "42" else '0';
  o(1) <= '1' when hex_text = "A5" else '0';
  o(2) <= '1' when bool_text = "true" else '0';
  o(3) <= '1' when to_string(v) = "101100" else '0';
  o(4) <= '1' when to_hstring(s) = "F8" else '0';
  o(5) <= '1' when to_ostring(u) = "5" else '0';
  o(6) <= '1' when to_string(b) = "1" else '0';
  o(7) <= '1' when to_hstring(v) = "2C" else '0';
end architecture rtl;
