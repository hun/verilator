-- DESCRIPTION: Verilator: VHDL linkage port mode is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
--
-- The linkage port mode is not implemented by the native VHDL frontend.

entity t is
  port (p : linkage bit);
end entity t;

architecture rtl of t is
begin
end architecture rtl;
