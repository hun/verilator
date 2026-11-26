-- DESCRIPTION: Verilator: VHDL real ports, top level and through an instance
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
entity sub is
  port (x : in real; y : out real);
end entity;
architecture rtl of sub is
begin
  y <= x * 2.0 + 0.5;
end architecture;
library ieee;
use ieee.std_logic_1164.all;
entity t is
  port (p : in real; q : out real; g : out std_logic; h : out real);
end entity t;
architecture rtl of t is
  component sub port (x : in real; y : out real); end component;
begin
  u : sub port map (x => p, y => q);
  g <= '1' when p > 0.5 else '0';
  h <= p + 1.0;
end architecture rtl;
