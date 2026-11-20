-- DESCRIPTION: Verilator: VHDL is_x in the 2-state model
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a : in  std_logic;
    b : in  std_logic_vector(3 downto 0);
    x : out std_logic;
    y : out std_logic
  );
end entity t;

architecture rtl of t is
begin
  -- No value is a metavalue in the 2-state model
  x <= '1' when is_x(a) else '0';
  y <= '1' when is_x(b) else '0';
end architecture rtl;
