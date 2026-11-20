-- DESCRIPTION: Verilator: VHDL std_logic_signed arithmetic is refused
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_signed.all;

entity t is
  port (
    a : in std_logic_vector(3 downto 0);
    b : in std_logic_vector(3 downto 0);
    o : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of t is
begin
  -- std_logic_signed makes this signed; the frontend does not model that yet
  o <= a + b;
end;
