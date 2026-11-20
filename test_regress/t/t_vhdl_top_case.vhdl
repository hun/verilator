-- DESCRIPTION: Verilator: VHDL a mixed-case --top-module name
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity CaseTop is
  port (y : out integer);
end;

architecture rtl of CaseTop is
begin
  y <= 1;
end;
