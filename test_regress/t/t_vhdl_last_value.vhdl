-- DESCRIPTION: Verilator: VHDL last_value of a signal that has an event
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t_vhdl_last_value is
  port (din  : in  std_logic;
        fall : out std_logic;
        rise : out std_logic;
        cnt  : out std_logic_vector(7 downto 0));
end t_vhdl_last_value;

architecture behav of t_vhdl_last_value is
begin
  -- A falling transition is an event whose previous value was '1'
  process (din)
    variable n : natural := 0;
  begin
    fall <= '0';
    rise <= '0';
    if din'event and din'last_value = '1' and din = '0' then
      fall <= '1';
      n := n + 1;
    elsif din'event and din'last_value = '0' and din = '1' then
      rise <= '1';
    end if;
    cnt <= std_logic_vector (to_unsigned (n, 8));
  end process;
end behav;
