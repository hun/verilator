-- DESCRIPTION: Verilator: VHDL subprogram parameter of mode linkage is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The linkage mode is legal VHDL for a subprogram parameter, but the frontend does not support it.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  procedure p (x : linkage bit);
begin
  o <= '1';
end architecture rtl;
