-- DESCRIPTION: Verilator: a delay in a conditional signal assignment has no model
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is end entity;
architecture a of t is
  signal clk : bit;
  signal running : boolean := true;
begin
  clk <= not clk after 5 ns when running else '0';
end architecture;
