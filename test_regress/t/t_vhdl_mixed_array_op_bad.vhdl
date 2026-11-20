-- DESCRIPTION: Verilator: VHDL array operators need one base type
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- unsigned and std_logic_vector have the same element type but are unrelated
-- array types (IEEE 1076-2008 9.2.2, 9.2.5).

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    u : in  unsigned(3 downto 0);
    v : in  std_logic_vector(3 downto 0);
    y : out std_logic_vector(7 downto 0)
  );
end;

architecture rtl of t is
begin
  y(3 downto 0) <= std_logic_vector(u + v);
  y(3 downto 0) <= u and v;
  y <= u & v;
end;
