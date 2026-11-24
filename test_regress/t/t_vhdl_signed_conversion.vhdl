-- DESCRIPTION: Verilator: signed operations on a converted std_logic_vector
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
  port (a : in std_logic_vector(3 downto 0);
        negative : out std_logic;
        above : out std_logic;
        shifted : out std_logic_vector(3 downto 0);
        extended : out std_logic_vector(5 downto 0);
        halved : out std_logic_vector(3 downto 0);
        magnitude : out std_logic_vector(3 downto 0));
end entity t;

architecture rtl of t is
begin
  -- signed(a) is a conversion, not a signed port: every operation has to see a signed
  -- operand, not the unsigned type of the variable the conversion started from
  negative <= '1' when signed(a) < 0 else '0';
  above <= '1' when signed(a) >= -2 else '0';
  shifted <= std_logic_vector(shift_right(signed(a), 1));
  extended <= std_logic_vector(resize(signed(a), 6));
  halved <= std_logic_vector(signed(a) / 2);
  magnitude <= std_logic_vector(abs signed(a));
end architecture rtl;
