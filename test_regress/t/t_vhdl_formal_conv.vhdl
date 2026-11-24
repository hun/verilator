-- DESCRIPTION: Verilator: a type conversion of a formal in a port map
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity inv is
  port (d : out std_logic_vector(7 downto 0); a : in std_logic_vector(7 downto 0));
end;

architecture r of inv is
begin
  d <= not a;
end;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t_vhdl_formal_conv is
  port (x : in unsigned(7 downto 0); y : out signed(7 downto 0));
end;

architecture r of t_vhdl_formal_conv is
  component inv is
    port (d : out std_logic_vector(7 downto 0); a : in std_logic_vector(7 downto 0));
  end component;
begin
  u : inv port map (signed(d) => y, std_logic_vector(a) => x);
end;
