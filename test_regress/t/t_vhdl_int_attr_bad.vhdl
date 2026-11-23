-- DESCRIPTION: Verilator: VHDL leftof of the last value of an integer subtype is an error
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is
  port (o : out bit);
end;

architecture rtl of t is
  subtype down is natural range 10 downto 1;
  constant beyond : integer := down'leftof(10);
begin
  o <= '0';
end;
